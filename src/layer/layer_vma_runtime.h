#pragma once

#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>

#include "vk_mem_alloc.h"

#include "layer_device_dispatch_types.h"
#include "layer_dispatch_types.h"
#include "layer_shared_types.h"

struct StagingChunkRange {
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
};

struct StagingChunk {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    void* mapped_data = nullptr;
    std::vector<StagingChunkRange> free_ranges;
};

struct VmaRuntime {
    std::mutex init_mutex;
    std::mutex staging_mutex;
    bool initialized = false;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaVulkanFunctions vulkan_functions{};
    VkDeviceSize min_storage_buffer_offset_alignment = 16;
    std::vector<StagingChunk> staging_chunks;
};

uint32_t clamp_vma_api_version(uint32_t api_version);

bool initialize_vma_runtime(
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkDevice device,
    const InstanceDispatch& instance_dispatch,
    const DeviceDispatch& dispatch,
    VmaRuntime* runtime);

void destroy_vma_runtime(VmaRuntime* runtime);
