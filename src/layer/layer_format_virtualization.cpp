#include "layer_format_virtualization.h"

#include <algorithm>
#include <mutex>

#if defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#endif

#include "layer_dispatch_key.h"
#include "layer_vk_struct_utils.h"

namespace {

constexpr VkImageUsageFlags kBcnVirtualImageInternalUsage =
    VK_IMAGE_USAGE_STORAGE_BIT |
    VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

constexpr VkImageCreateFlags kBcnVirtualUnsupportedImageFlags =
    VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
    VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
    VK_IMAGE_CREATE_DISJOINT_BIT;

VkFormatFeatureFlags required_format_features_for_usage(VkImageUsageFlags usage) {
    if (usage == 0) {
        usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VkFormatFeatureFlags required = 0;
    if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
        required |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
    if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
        required |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    }
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    }
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
        required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    }
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    }
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        required |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
        required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    }

    if (required == 0) {
        required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    }
    return required;
}

bool has_external_memory_create_request(const void* pNext) {
    for (auto* current = reinterpret_cast<const VkBaseInStructure*>(pNext);
         current;
         current = current->pNext) {
        switch (current->sType) {
            case VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO: {
                auto* external_info = reinterpret_cast<const VkExternalMemoryImageCreateInfo*>(current);
                if (external_info->handleTypes != 0) {
                    return true;
                }
                break;
            }
            case VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_NV: {
                auto* external_info = reinterpret_cast<const VkExternalMemoryImageCreateInfoNV*>(current);
                if (external_info->handleTypes != 0) {
                    return true;
                }
                break;
            }
#if defined(__ANDROID__)
            case VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID: {
                auto* external_format = reinterpret_cast<const VkExternalFormatANDROID*>(current);
                if (external_format->externalFormat != 0) {
                    return true;
                }
                break;
            }
#endif
            default:
                break;
        }
    }
    return false;
}

bool wants_explicit_format_compatibility_list(VkImageCreateFlags flags) {
    return (flags & (VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                     VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT)) != 0;
}

bool append_unique_format(std::vector<VkFormat>* formats, VkFormat format) {
    if (!formats || format == VK_FORMAT_UNDEFINED) {
        return false;
    }
    if (std::find(formats->begin(), formats->end(), format) != formats->end()) {
        return false;
    }
    formats->push_back(format);
    return true;
}

void append_unorm_srgb_compatibility_class(std::vector<VkFormat>* formats, VkFormat format) {
    if (!formats) {
        return;
    }
    switch (format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SRGB:
            append_unique_format(formats, VK_FORMAT_R8_UNORM);
            append_unique_format(formats, VK_FORMAT_R8_SRGB);
            break;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SRGB:
            append_unique_format(formats, VK_FORMAT_R8G8_UNORM);
            append_unique_format(formats, VK_FORMAT_R8G8_SRGB);
            break;
        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SRGB:
            append_unique_format(formats, VK_FORMAT_R8G8B8_UNORM);
            append_unique_format(formats, VK_FORMAT_R8G8B8_SRGB);
            break;
        case VK_FORMAT_B8G8R8_UNORM:
        case VK_FORMAT_B8G8R8_SRGB:
            append_unique_format(formats, VK_FORMAT_B8G8R8_UNORM);
            append_unique_format(formats, VK_FORMAT_B8G8R8_SRGB);
            break;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            append_unique_format(formats, VK_FORMAT_R8G8B8A8_UNORM);
            append_unique_format(formats, VK_FORMAT_R8G8B8A8_SRGB);
            break;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            append_unique_format(formats, VK_FORMAT_B8G8R8A8_UNORM);
            append_unique_format(formats, VK_FORMAT_B8G8R8A8_SRGB);
            break;
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
            append_unique_format(formats, VK_FORMAT_A8B8G8R8_UNORM_PACK32);
            append_unique_format(formats, VK_FORMAT_A8B8G8R8_SRGB_PACK32);
            break;
        default:
            break;
    }
}

bool query_image_format_support_uncached(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags) {
    if (format == VK_FORMAT_UNDEFINED || !dispatch.get_physical_device_format_properties) {
        return false;
    }

    VkFormatProperties format_props{};
    dispatch.get_physical_device_format_properties(physicalDevice, format, &format_props);
    VkFormatFeatureFlags required = required_format_features_for_usage(usage);
    VkFormatFeatureFlags available = (tiling == VK_IMAGE_TILING_LINEAR)
        ? format_props.linearTilingFeatures
        : format_props.optimalTilingFeatures;
    if ((available & required) != required) {
        return false;
    }

    if (dispatch.get_physical_device_image_format_properties2) {
        VkPhysicalDeviceImageFormatInfo2 image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
        image_info.format = format;
        image_info.type = type;
        image_info.tiling = tiling;
        image_info.usage = usage;
        image_info.flags = flags;

        VkImageFormatProperties2 image_props{};
        image_props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
        return dispatch.get_physical_device_image_format_properties2(
            physicalDevice,
            &image_info,
            &image_props) == VK_SUCCESS;
    }

    if (dispatch.get_physical_device_image_format_properties) {
        VkImageFormatProperties image_props{};
        return dispatch.get_physical_device_image_format_properties(
            physicalDevice,
            format,
            type,
            tiling,
            usage,
            flags,
            &image_props) == VK_SUCCESS;
    }

    return true;
}

bool query_native_bcn_support_uncached(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags) {
    if (!is_bcn_format(format)) {
        return true;
    }
    if (!dispatch.get_physical_device_format_properties) {
        return true;
    }

    return query_image_format_support_uncached(
        physicalDevice,
        dispatch,
        format,
        type,
        tiling,
        usage,
        flags);
}

}  // namespace

size_t BcnSupportKeyHash::operator()(const BcnSupportKey& key) const {
    size_t h = reinterpret_cast<size_t>(key.physical);
    h ^= static_cast<size_t>(static_cast<uint32_t>(key.format) + 0x9e3779b9u + (h << 6) + (h >> 2));
    h ^= static_cast<size_t>(static_cast<uint32_t>(key.type) + 0x9e3779b9u + (h << 6) + (h >> 2));
    h ^= static_cast<size_t>(static_cast<uint32_t>(key.tiling) + 0x9e3779b9u + (h << 6) + (h >> 2));
    h ^= static_cast<size_t>(key.usage + 0x9e3779b9u + (h << 6) + (h >> 2));
    h ^= static_cast<size_t>(key.flags + 0x9e3779b9u + (h << 6) + (h >> 2));
    return h;
}

bool is_bcn_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

uint32_t bcn_format_mask_bit(VkFormat format) {
    if (!is_bcn_format(format)) {
        return 0;
    }
    const uint32_t format_index =
        static_cast<uint32_t>(format) - static_cast<uint32_t>(VK_FORMAT_BC1_RGB_UNORM_BLOCK);
    if (format_index >= 32u) {
        return 0;
    }
    return 1u << format_index;
}

bool is_bcn_srgb_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

bool is_bcn_unorm_srgb_pair_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

bool is_bcn_force_emulation_candidate(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

VkFormat bcn_srgb_variant(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
            return VK_FORMAT_BC2_SRGB_BLOCK;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        default:
            return format;
    }
}

VkFormat bcn_unorm_variant(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        default:
            return format;
    }
}

VkFormat bcn_fallback_replacement_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

VkFormat srgb_view_compatible_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        default:
            return format;
    }
}

VkFormat unorm_view_compatible_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return VK_FORMAT_B8G8R8A8_UNORM;
        default:
            return format;
    }
}

VkImageCreateFlags bcn_replacement_image_create_flags(VkImageType type, VkImageCreateFlags flags) {
    VkImageCreateFlags replacement_flags = flags & ~VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
    if (type == VK_IMAGE_TYPE_3D) {
        replacement_flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
    }
    return replacement_flags;
}

VkFormat bcn_replacement_format(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags) {
    VkImageUsageFlags replacement_usage =
        usage |
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto supports_candidate = [&](VkFormat candidate) {
        if (candidate == VK_FORMAT_UNDEFINED || physicalDevice == VK_NULL_HANDLE) {
            return false;
        }
        return query_image_format_support_uncached(
            physicalDevice,
            dispatch,
            candidate,
            type,
            tiling,
            replacement_usage,
            flags);
    };

    switch (format) {
        case VK_FORMAT_BC4_UNORM_BLOCK:
            if (supports_candidate(VK_FORMAT_R8_UNORM)) {
                return VK_FORMAT_R8_UNORM;
            }
            break;
        case VK_FORMAT_BC4_SNORM_BLOCK:
            if (supports_candidate(VK_FORMAT_R8_SNORM)) {
                return VK_FORMAT_R8_SNORM;
            }
            break;
        case VK_FORMAT_BC5_UNORM_BLOCK:
            if (supports_candidate(VK_FORMAT_R8G8_UNORM)) {
                return VK_FORMAT_R8G8_UNORM;
            }
            break;
        case VK_FORMAT_BC5_SNORM_BLOCK:
            if (supports_candidate(VK_FORMAT_R8G8_SNORM)) {
                return VK_FORMAT_R8G8_SNORM;
            }
            break;
        default:
            break;
    }

    return bcn_fallback_replacement_format(format);
}

bool is_native_bcn_supported(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    std::shared_mutex& lock,
    std::unordered_map<BcnSupportKey, bool, BcnSupportKeyHash>& native_support_cache) {
    if (!is_bcn_format(format)) {
        return true;
    }

    BcnSupportKey key{};
    key.physical = dispatch_key(physicalDevice);
    key.format = format;
    key.type = type;
    key.tiling = tiling;
    key.usage = usage;
    key.flags = flags;

    {
        std::shared_lock<std::shared_mutex> guard(lock);
        auto it = native_support_cache.find(key);
        if (it != native_support_cache.end()) {
            return it->second;
        }
    }

    bool native_supported = query_native_bcn_support_uncached(
        physicalDevice,
        dispatch,
        format,
        type,
        tiling,
        usage,
        flags);

    {
        std::lock_guard<std::shared_mutex> guard(lock);
        native_support_cache[key] = native_supported;
    }
    return native_supported;
}

bool should_virtualize_bcn_format(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    const VirtualizationPolicySettings& settings,
    bool is_xclipse_physical,
    std::shared_mutex& lock,
    std::unordered_map<BcnSupportKey, bool, BcnSupportKeyHash>& native_support_cache) {
    if (!settings.enabled || !settings.bcn_intercept) {
        return false;
    }
    if (!is_bcn_format(format)) {
        return false;
    }
    if ((settings.disabled_bcn_mask & bcn_format_mask_bit(format)) != 0u) {
        return false;
    }
    if (type != VK_IMAGE_TYPE_2D && type != VK_IMAGE_TYPE_3D) {
        return false;
    }
    if (settings.xclipse_only && !is_xclipse_physical) {
        return false;
    }
    if (settings.force_bcn_emulation &&
        is_xclipse_physical &&
        is_bcn_force_emulation_candidate(format)) {
        return true;
    }

    return !is_native_bcn_supported(
        physicalDevice,
        dispatch,
        format,
        type,
        tiling,
        usage,
        flags,
        lock,
        native_support_cache);
}

bool can_virtualize_bcn_image_create_info(const VkImageCreateInfo* create_info) {
    if (!create_info || !is_bcn_format(create_info->format)) {
        return false;
    }
    if (create_info->imageType != VK_IMAGE_TYPE_2D &&
        create_info->imageType != VK_IMAGE_TYPE_3D) {
        return false;
    }
    if (create_info->extent.width == 0 ||
        create_info->extent.height == 0 ||
        create_info->extent.depth == 0 ||
        create_info->mipLevels == 0 ||
        create_info->arrayLayers == 0) {
        return false;
    }
    if (create_info->samples != VK_SAMPLE_COUNT_1_BIT) {
        return false;
    }
    if ((create_info->flags & kBcnVirtualUnsupportedImageFlags) != 0) {
        return false;
    }
    if ((create_info->usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0) {
        return false;
    }
    if ((create_info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0 &&
        (create_info->imageType != VK_IMAGE_TYPE_2D ||
         create_info->extent.depth != 1 ||
         create_info->extent.width != create_info->extent.height ||
         create_info->arrayLayers < 6)) {
        return false;
    }
    if ((create_info->flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT) != 0 &&
        create_info->imageType != VK_IMAGE_TYPE_3D) {
        return false;
    }
    if ((create_info->flags & VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT) != 0 &&
        create_info->imageType != VK_IMAGE_TYPE_3D) {
        return false;
    }
    if (create_info->imageType == VK_IMAGE_TYPE_2D && create_info->extent.depth != 1) {
        return false;
    }
    if (create_info->imageType == VK_IMAGE_TYPE_3D && create_info->arrayLayers != 1) {
        return false;
    }
    if (has_external_memory_create_request(create_info->pNext)) {
        return false;
    }
    return true;
}

bool patch_virtualized_image_format_list(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    const VkImageCreateInfo* original_info,
    VkImageCreateInfo* io_patched_info,
    PatchedImageFormatListChain* out_patch) {
#ifdef VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO
    if (!original_info || !io_patched_info || !out_patch) {
        return false;
    }

    VkBaseOutStructure* previous = nullptr;
    auto* format_list = find_struct_in_pnext_chain<VkImageFormatListCreateInfo>(
        const_cast<void*>(io_patched_info->pNext),
        VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        &previous);
    if (!format_list && !wants_explicit_format_compatibility_list(original_info->flags)) {
        return false;
    }

    out_patch->patched_view_formats.clear();
    out_patch->patched_view_formats.reserve(format_list ? format_list->viewFormatCount + 8 : 8);
    append_unique_format(&out_patch->patched_view_formats, io_patched_info->format);
    append_unorm_srgb_compatibility_class(
        &out_patch->patched_view_formats,
        io_patched_info->format);

    if (is_bcn_unorm_srgb_pair_format(original_info->format)) {
        VkFormat paired_format = is_bcn_srgb_format(original_info->format)
            ? bcn_unorm_variant(original_info->format)
            : bcn_srgb_variant(original_info->format);
        paired_format = bcn_replacement_format(
            physicalDevice,
            dispatch,
            paired_format,
            original_info->imageType,
            original_info->tiling,
            original_info->usage,
            io_patched_info->flags);
        append_unique_format(&out_patch->patched_view_formats, paired_format);
        append_unique_format(
            &out_patch->patched_view_formats,
            srgb_view_compatible_format(io_patched_info->format));
        append_unique_format(
            &out_patch->patched_view_formats,
            unorm_view_compatible_format(io_patched_info->format));
    }

    if (format_list && format_list->pViewFormats && format_list->viewFormatCount != 0) {
        for (uint32_t i = 0; i < format_list->viewFormatCount; ++i) {
            VkFormat view_format = format_list->pViewFormats[i];
            if (is_bcn_format(view_format)) {
                view_format = bcn_replacement_format(
                    physicalDevice,
                    dispatch,
                    view_format,
                    original_info->imageType,
                    original_info->tiling,
                    original_info->usage,
                io_patched_info->flags);
            }
            append_unique_format(&out_patch->patched_view_formats, view_format);
            append_unorm_srgb_compatibility_class(
                &out_patch->patched_view_formats,
                view_format);

            if (is_bcn_unorm_srgb_pair_format(format_list->pViewFormats[i])) {
                VkFormat paired_view_format = is_bcn_srgb_format(format_list->pViewFormats[i])
                    ? bcn_unorm_variant(format_list->pViewFormats[i])
                    : bcn_srgb_variant(format_list->pViewFormats[i]);
                paired_view_format = bcn_replacement_format(
                physicalDevice,
                dispatch,
                paired_view_format,
                original_info->imageType,
                original_info->tiling,
                original_info->usage,
                io_patched_info->flags);
                append_unique_format(&out_patch->patched_view_formats, paired_view_format);
                append_unique_format(
                    &out_patch->patched_view_formats,
                    srgb_view_compatible_format(view_format));
                append_unique_format(
                    &out_patch->patched_view_formats,
                    unorm_view_compatible_format(view_format));
            }
        }
    }

    if (out_patch->patched_view_formats.empty()) {
        return false;
    }

    out_patch->patched_format_list.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
    out_patch->patched_format_list.pNext = format_list ? format_list->pNext : io_patched_info->pNext;
    out_patch->patched_format_list.viewFormatCount =
        static_cast<uint32_t>(out_patch->patched_view_formats.size());
    out_patch->patched_format_list.pViewFormats =
        out_patch->patched_view_formats.data();
    out_patch->original_format_list = format_list;
    out_patch->previous = previous;
    out_patch->inserted_head = (format_list == nullptr);
    out_patch->replaced_head = (format_list != nullptr && previous == nullptr);

    if (out_patch->inserted_head) {
        io_patched_info->pNext = &out_patch->patched_format_list;
    } else if (previous) {
        previous->pNext = reinterpret_cast<VkBaseOutStructure*>(&out_patch->patched_format_list);
    } else {
        io_patched_info->pNext = &out_patch->patched_format_list;
    }
    return true;
#else
    (void)physicalDevice;
    (void)dispatch;
    (void)original_info;
    (void)io_patched_info;
    (void)out_patch;
    return false;
#endif
}

void restore_virtualized_image_format_list(
    VkImageCreateInfo* io_patched_info,
    PatchedImageFormatListChain* patch) {
#ifdef VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO
    if (!io_patched_info || !patch) {
        return;
    }

    if (patch->inserted_head) {
        io_patched_info->pNext = patch->patched_format_list.pNext;
    } else if (patch->replaced_head) {
        io_patched_info->pNext = patch->original_format_list;
    } else if (patch->previous) {
        patch->previous->pNext = patch->original_format_list;
    }

    patch->original_format_list = nullptr;
    patch->previous = nullptr;
    patch->replaced_head = false;
    patch->inserted_head = false;
    patch->patched_view_formats.clear();
#else
    (void)io_patched_info;
    (void)patch;
#endif
}
