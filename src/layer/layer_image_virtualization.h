#pragma once

#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include <vulkan/utility/vk_safe_struct.hpp>

#include "layer_device_dispatch_types.h"
#include "layer_dispatch_types.h"
#include "layer_format_virtualization.h"
#include "layer_shared_types.h"

struct VirtualizedImageCreateResult {
    bool handled = false;
    bool virtualized = false;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    VkFormat replacement = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags actual_usage = 0;
    VkImageCreateFlags actual_flags = 0;
};

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
    VirtualizedImageCreateResult* out_result);

void track_created_image(
    VkDevice device,
    VkImage image,
    const VkImageCreateInfo* pCreateInfo,
    std::shared_mutex& lock,
    std::unordered_map<void*, TrackedImageInfo>& tracked_images,
    std::unordered_map<void*, void*>& image_to_device,
    std::unordered_map<void*, DecodeImageState>& decode_image_state);

bool create_virtualized_image_view(
    VkDevice device,
    const DeviceDispatch& device_dispatch,
    const VkImageViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImageView* pView,
    std::shared_mutex& lock,
    std::unordered_map<void*, VirtualImageInfo>& virtual_images,
    VkResult* out_result);

void cleanup_destroyed_image_tracking(
    VkImage image,
    std::shared_mutex& lock,
    std::unordered_map<void*, VirtualImageInfo>& virtual_images,
    std::unordered_map<void*, TrackedImageInfo>& tracked_images,
    std::unordered_map<void*, void*>& image_to_device,
    std::unordered_map<void*, DecodeImageState>& decode_image_state,
    std::unordered_map<StorageViewKey, VkImageView, StorageViewKeyHash>& storage_views,
    std::vector<VkImageView>* out_storage_views_to_destroy);
