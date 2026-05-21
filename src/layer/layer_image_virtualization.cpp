#include "layer_image_virtualization.h"

#include "layer_dispatch_key.h"
#include "layer_vk_struct_clone.h"

namespace {

}  // namespace

bool try_create_virtualized_image(
    VkDevice device,
    VkPhysicalDevice physical_device,
    const DeviceDispatch& device_dispatch,
    const InstanceDispatch& instance_dispatch,
    const VirtualizationPolicySettings& settings,
    bool is_xclipse_physical,
    const VkImageCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImage* pImage,
    std::shared_mutex& lock,
    std::unordered_map<BcnSupportKey, bool, BcnSupportKeyHash>& native_support_cache,
    std::unordered_map<void*, VirtualImageInfo>& virtual_images,
    std::unordered_map<void*, TrackedImageInfo>& tracked_images,
    std::unordered_map<void*, void*>& image_to_device,
    std::unordered_map<void*, DecodeImageState>& decode_image_state,
    VirtualizedImageCreateResult* out_result) {
    if (!out_result) {
        return false;
    }

    *out_result = {};
    if (physical_device == VK_NULL_HANDLE || !pCreateInfo || !pImage || !device_dispatch.create_image) {
        return false;
    }
    if (!is_bcn_format(pCreateInfo->format)) {
        return false;
    }
    if (!can_virtualize_bcn_image_create_info(pCreateInfo)) {
        return false;
    }

    if (!should_virtualize_bcn_format(
            physical_device,
            instance_dispatch,
            pCreateInfo->format,
            pCreateInfo->imageType,
            pCreateInfo->tiling,
            pCreateInfo->usage,
            pCreateInfo->flags,
            settings,
            is_xclipse_physical,
            lock,
            native_support_cache)) {
        return false;
    }

    VkImageCreateFlags replacement_flags =
        bcn_replacement_image_create_flags(pCreateInfo->imageType, pCreateInfo->flags);
    if (is_bcn_unorm_srgb_pair_format(pCreateInfo->format)) {
        replacement_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    }
    VkFormat replacement = bcn_replacement_format(
        physical_device,
        instance_dispatch,
        pCreateInfo->format,
        pCreateInfo->imageType,
        pCreateInfo->tiling,
        pCreateInfo->usage,
        replacement_flags);
    if (replacement == VK_FORMAT_UNDEFINED) {
        return false;
    }

    auto patched_info_safe = clone_image_create_info(pCreateInfo);
    VkImageCreateInfo* patched_info = patched_info_safe.ptr();
    PatchedImageFormatListChain patched_format_list_chain{};
    patched_info->format = replacement;
    patched_info->usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    patched_info->usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    patched_info->usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    patched_info->usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    patched_info->flags = replacement_flags;
    patch_virtualized_image_format_list(
        physical_device,
        instance_dispatch,
        pCreateInfo,
        patched_info,
        &patched_format_list_chain);

    VkResult result = device_dispatch.create_image(device, patched_info, pAllocator, pImage);
    restore_virtualized_image_format_list(patched_info, &patched_format_list_chain);

    out_result->handled = true;
    out_result->result = result;
    out_result->replacement = replacement;
    out_result->actual_usage = patched_info->usage;
    out_result->actual_flags = patched_info->flags;

    if (result == VK_SUCCESS && *pImage != VK_NULL_HANDLE) {
        std::lock_guard<std::shared_mutex> guard(lock);
        auto image_key = dispatch_key(*pImage);
        VirtualImageInfo virtual_info{};
        virtual_info.requested_format = pCreateInfo->format;
        virtual_info.decode_format = pCreateInfo->format;
        virtual_info.real_format = replacement;
        virtual_info.image_type = pCreateInfo->imageType;
        virtual_info.has_srgb_view = is_bcn_srgb_format(pCreateInfo->format);
        virtual_info.has_unorm_view =
            is_bcn_unorm_srgb_pair_format(pCreateInfo->format) &&
            !is_bcn_srgb_format(pCreateInfo->format);
        virtual_images[image_key] = virtual_info;
        tracked_images[image_key] =
            TrackedImageInfo{replacement, patched_info->imageType, patched_info->usage, patched_info->flags};
        image_to_device[image_key] = dispatch_key(device);
        decode_image_state.erase(image_key);
        out_result->virtualized = true;
    }

    return true;
}

void track_created_image(
    VkDevice device,
    VkImage image,
    const VkImageCreateInfo* pCreateInfo,
    std::shared_mutex& lock,
    std::unordered_map<void*, TrackedImageInfo>& tracked_images,
    std::unordered_map<void*, void*>& image_to_device,
    std::unordered_map<void*, DecodeImageState>& decode_image_state) {
    if (image == VK_NULL_HANDLE || !pCreateInfo) {
        return;
    }

    std::lock_guard<std::shared_mutex> guard(lock);
    auto image_key = dispatch_key(image);
    tracked_images[image_key] =
        TrackedImageInfo{pCreateInfo->format, pCreateInfo->imageType, pCreateInfo->usage, pCreateInfo->flags};
    image_to_device[image_key] = dispatch_key(device);
    decode_image_state.erase(image_key);
}

bool create_virtualized_image_view(
    VkDevice device,
    const DeviceDispatch& device_dispatch,
    const VkImageViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImageView* pView,
    std::shared_mutex& lock,
    std::unordered_map<void*, VirtualImageInfo>& virtual_images,
    VkResult* out_result) {
    if (!out_result || !device_dispatch.create_image_view || !pCreateInfo) {
        return false;
    }

    void* image_key = dispatch_key(pCreateInfo->image);
    VirtualImageInfo virtual_info{};
    {
        std::shared_lock<std::shared_mutex> guard(lock);
        auto it_image = virtual_images.find(image_key);
        if (it_image == virtual_images.end()) {
            return false;
        }
        virtual_info = it_image->second;
    }

    auto patched_view_safe = clone_image_view_create_info(pCreateInfo);
    VkImageViewCreateInfo* patched_view = patched_view_safe.ptr();
    VkFormat requested_view_format = pCreateInfo->format;
    if (patched_view->format == virtual_info.requested_format || is_bcn_format(patched_view->format)) {
        patched_view->format = virtual_info.real_format;
        if (is_bcn_srgb_format(requested_view_format)) {
            patched_view->format = srgb_view_compatible_format(virtual_info.real_format);
        } else if (is_bcn_unorm_srgb_pair_format(requested_view_format)) {
            patched_view->format = unorm_view_compatible_format(virtual_info.real_format);
        }
    }
    *out_result = device_dispatch.create_image_view(device, patched_view, pAllocator, pView);
    if (*out_result == VK_SUCCESS &&
        pView &&
        *pView != VK_NULL_HANDLE &&
        is_bcn_unorm_srgb_pair_format(requested_view_format)) {
        std::lock_guard<std::shared_mutex> guard(lock);
        auto it_image = virtual_images.find(image_key);
        if (it_image != virtual_images.end()) {
            if (is_bcn_srgb_format(requested_view_format)) {
                it_image->second.has_srgb_view = true;
                it_image->second.decode_format = bcn_srgb_variant(it_image->second.requested_format);
            } else if (!it_image->second.has_srgb_view) {
                it_image->second.has_unorm_view = true;
                it_image->second.decode_format = bcn_unorm_variant(it_image->second.requested_format);
            } else {
                it_image->second.has_unorm_view = true;
            }
        }
    }
    return true;
}

void cleanup_destroyed_image_tracking(
    VkImage image,
    std::shared_mutex& lock,
    std::unordered_map<void*, VirtualImageInfo>& virtual_images,
    std::unordered_map<void*, TrackedImageInfo>& tracked_images,
    std::unordered_map<void*, void*>& image_to_device,
    std::unordered_map<void*, DecodeImageState>& decode_image_state,
    std::unordered_map<StorageViewKey, VkImageView, StorageViewKeyHash>& storage_views,
    std::vector<VkImageView>* out_storage_views_to_destroy) {
    if (image == VK_NULL_HANDLE || !out_storage_views_to_destroy) {
        return;
    }

    std::lock_guard<std::shared_mutex> guard(lock);
    auto image_key = dispatch_key(image);
    virtual_images.erase(image_key);
    tracked_images.erase(image_key);
    decode_image_state.erase(image_key);
    image_to_device.erase(image_key);
    for (auto view_it = storage_views.begin(); view_it != storage_views.end();) {
        if (view_it->first.image == image_key) {
            out_storage_views_to_destroy->push_back(view_it->second);
            view_it = storage_views.erase(view_it);
        } else {
            ++view_it;
        }
    }
}
