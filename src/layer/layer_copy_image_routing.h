#pragma once

#include <atomic>
#include <shared_mutex>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "layer_shared_types.h"

struct CopyImageRouteInfo {
    bool involves_virtual = false;
    bool can_copy_real_images = false;
    bool needs_special_path = false;
    bool can_use_special_path = false;
    VkFormat src_actual_format = VK_FORMAT_UNDEFINED;
    VkFormat dst_actual_format = VK_FORMAT_UNDEFINED;
    VkImageType src_type = VK_IMAGE_TYPE_2D;
    VkImageType dst_type = VK_IMAGE_TYPE_2D;
    VkImageUsageFlags src_usage = 0;
    VkImageUsageFlags dst_usage = 0;
};

CopyImageRouteInfo classify_copy_image_route(
    VkImage src_image,
    VkImage dst_image,
    std::shared_mutex& lock,
    const std::unordered_map<void*, VirtualImageInfo>& virtual_images,
    const std::unordered_map<void*, TrackedImageInfo>& tracked_images);

void note_copy_image_route(
    const char* api_name,
    const CopyImageRouteInfo& route,
    std::atomic<uint64_t>& copy_image_calls,
    std::atomic<uint64_t>& copy_image_virtual_hits,
    std::atomic<uint64_t>& copy_image_real_routes,
    std::atomic<uint64_t>& copy_image_special_fallbacks,
    void (*log_decode_stats_fn)(),
    void (*log_warn_fn)(const char*, int, int));
