#pragma once

#include <string>

#include <vulkan/vulkan.h>

#include "vk_mem_alloc.h"

struct VirtualImageInfo {
    VkFormat requested_format = VK_FORMAT_UNDEFINED;
    VkFormat decode_format = VK_FORMAT_UNDEFINED;
    VkFormat real_format = VK_FORMAT_UNDEFINED;
    VkImageType image_type = VK_IMAGE_TYPE_2D;
    bool has_srgb_view = false;
    bool has_unorm_view = false;
};

struct TrackedImageInfo {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;
    bool is_depth_stencil_reduced = false;
    VkFormat original_depth_format = VK_FORMAT_UNDEFINED;
};

struct TrackedBufferBinding {
    VkDevice device = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize memory_offset = 0;
};

struct TrackedMemoryMap {
    VkDevice device = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* data = nullptr;
};

struct InstanceRuntime {
    std::string application_name;
    uint32_t application_version = 0;
    std::string engine_name;
    uint32_t engine_version = 0;
    uint32_t api_version = 0;
    bool is_dxvk = false;
    bool is_dxvk_2_or_newer = false;
    bool is_vkd3d_proton = false;
    bool is_clvk = false;
    // GameNative's lsfg-vk-android frame-generation layer creates its own
    // internal Vulkan instance/device with application/engine name
    // "lsfg-vk-base". That device only runs Lossless Scaling framegen on
    // ordinary color (RGBA8/RGBA16F) images and never feeds BCn textures
    // through the layer, so it does not need the BCn compute runtime. This flag
    // identifies that internal device so ExynosTools can keep its BCn fallback
    // active for ordinary application devices while leaving the LSFG internal
    // device untouched.
    bool is_lsfg_framegen = false;
};

struct DeviceRuntime {
    InstanceRuntime app{};
    bool is_xclipse = false;
    uint32_t vendor_id = 0;
    VkDriverId driver_id = VK_DRIVER_ID_MAX_ENUM;
    bool geometry_shader = false;
    bool tessellation_shader = false;
    bool transform_feedback = false;
    bool shader_storage_image_write_without_format = false;
    bool subgroup_size_control = false;
    uint32_t min_subgroup_size = 0;
    uint32_t max_subgroup_size = 0;
    bool descriptor_buffer_supported = false;
    bool descriptor_buffer_enabled = false;
    float timestamp_period = 0.0f;
    VkDeviceSize min_storage_buffer_offset_alignment = 16;
    VkDeviceSize descriptor_buffer_offset_alignment = 16;
    size_t storage_image_descriptor_size = 0;
    size_t storage_buffer_descriptor_size = 0;
    size_t combined_image_sampler_descriptor_size = 0;
};

struct PhysicalRuntime {
    bool is_xclipse = false;
    uint32_t vendor_id = 0;
    VkDriverId driver_id = VK_DRIVER_ID_MAX_ENUM;
    bool virtual_bc_feature_cached = false;
    bool virtual_bc_feature_enabled = false;
};

struct DecodeImageState {
    bool blocked_passthrough = false;
    uint32_t blocked_copy_count = 0;
    uint32_t failure_count = 0;
};

enum class DescriptorSetCacheKind : uint32_t {
    None = 0,
    Decode,
    SpecialCopy,
};

struct DecodeDescriptorSetKey {
    VkImageView storage_view = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = 0;

    bool operator==(const DecodeDescriptorSetKey& other) const {
        return storage_view == other.storage_view &&
               buffer == other.buffer &&
               offset == other.offset &&
               range == other.range;
    }
};

struct DecodeDescriptorSetKeyHash {
    size_t operator()(const DecodeDescriptorSetKey& key) const {
        size_t h = reinterpret_cast<size_t>(key.storage_view);
        h ^= reinterpret_cast<size_t>(key.buffer) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(key.offset + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(key.range + 0x9e3779b9u + (h << 6) + (h >> 2));
        return h;
    }
};

struct SpecialCopyDescriptorSetKey {
    VkImageView dst_view = VK_NULL_HANDLE;
    VkImageView src_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    bool operator==(const SpecialCopyDescriptorSetKey& other) const {
        return dst_view == other.dst_view &&
               src_view == other.src_view &&
               sampler == other.sampler;
    }
};

struct SpecialCopyDescriptorSetKeyHash {
    size_t operator()(const SpecialCopyDescriptorSetKey& key) const {
        size_t h = reinterpret_cast<size_t>(key.dst_view);
        h ^= reinterpret_cast<size_t>(key.src_view) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= reinterpret_cast<size_t>(key.sampler) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

struct DescriptorSetCacheRecord {
    DescriptorSetCacheKind kind = DescriptorSetCacheKind::None;
    DecodeDescriptorSetKey decode_key{};
    SpecialCopyDescriptorSetKey special_copy_key{};
};

struct TrackedDescriptorSet {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

struct StagingAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    bool dedicated = false;
};

struct StorageViewKey {
    void* image = nullptr;
    uint32_t mip_level = 0;
    uint32_t layer = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;

    bool operator==(const StorageViewKey& other) const {
        return image == other.image &&
               mip_level == other.mip_level &&
               layer == other.layer &&
               format == other.format;
    }
};

struct StorageViewKeyHash {
    size_t operator()(const StorageViewKey& key) const {
        size_t h = reinterpret_cast<size_t>(key.image);
        h ^= static_cast<size_t>(key.mip_level + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(key.layer + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(static_cast<uint32_t>(key.format) + 0x9e3779b9u + (h << 6) + (h >> 2));
        return h;
    }
};
