#include "layer_copy_image_routing.h"
#include "layer_dispatch_key.h"

CopyImageRouteInfo classify_copy_image_route(
    VkImage src_image,
    VkImage dst_image,
    std::shared_mutex& lock,
    const std::unordered_map<void*, VirtualImageInfo>& virtual_images,
    const std::unordered_map<void*, TrackedImageInfo>& tracked_images) {
    CopyImageRouteInfo route{};

    std::shared_lock<std::shared_mutex> guard(lock);
    void* src_key = dispatch_key(src_image);
    void* dst_key = dispatch_key(dst_image);

    auto src_virtual_it = virtual_images.find(src_key);
    auto dst_virtual_it = virtual_images.find(dst_key);
    route.involves_virtual =
        (src_virtual_it != virtual_images.end()) || (dst_virtual_it != virtual_images.end());

    auto src_info_it = tracked_images.find(src_key);
    if (src_info_it != tracked_images.end()) {
        route.src_actual_format = src_info_it->second.format;
        route.src_type = src_info_it->second.type;
        route.src_usage = src_info_it->second.usage;
    } else if (src_virtual_it != virtual_images.end()) {
        route.src_actual_format = src_virtual_it->second.real_format;
    }

    auto dst_info_it = tracked_images.find(dst_key);
    if (dst_info_it != tracked_images.end()) {
        route.dst_actual_format = dst_info_it->second.format;
        route.dst_type = dst_info_it->second.type;
        route.dst_usage = dst_info_it->second.usage;
    } else if (dst_virtual_it != virtual_images.end()) {
        route.dst_actual_format = dst_virtual_it->second.real_format;
    }

    if (!route.involves_virtual) {
        route.can_copy_real_images = true;
        return route;
    }

    if (route.src_actual_format != VK_FORMAT_UNDEFINED &&
        route.dst_actual_format != VK_FORMAT_UNDEFINED &&
        route.src_actual_format == route.dst_actual_format) {
        route.can_copy_real_images = true;
        return route;
    }

    route.needs_special_path = true;
    route.can_use_special_path =
        route.src_actual_format != VK_FORMAT_UNDEFINED &&
        route.dst_actual_format != VK_FORMAT_UNDEFINED &&
        route.src_type == VK_IMAGE_TYPE_2D &&
        route.dst_type == VK_IMAGE_TYPE_2D &&
        (route.src_usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 &&
        (route.dst_usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
    return route;
}

void note_copy_image_route(
    const char* api_name,
    const CopyImageRouteInfo& route,
    std::atomic<uint64_t>& copy_image_calls,
    std::atomic<uint64_t>& copy_image_virtual_hits,
    std::atomic<uint64_t>& copy_image_real_routes,
    std::atomic<uint64_t>& copy_image_special_fallbacks,
    void (*log_decode_stats_fn)(),
    void (*log_warn_fn)(const char*, int, int)) {
    copy_image_calls.fetch_add(1);
    if (!route.involves_virtual) {
        log_decode_stats_fn();
        return;
    }

    copy_image_virtual_hits.fetch_add(1);
    if (route.can_copy_real_images) {
        copy_image_real_routes.fetch_add(1);
        log_decode_stats_fn();
        return;
    }

    uint64_t fallback_count = copy_image_special_fallbacks.fetch_add(1) + 1;
    if (fallback_count <= 4u || (fallback_count % 64u) == 0u) {
        log_warn_fn(
            api_name,
            static_cast<int>(route.src_actual_format),
            static_cast<int>(route.dst_actual_format));
    }
    log_decode_stats_fn();
}
