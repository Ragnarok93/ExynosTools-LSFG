#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "vk_mem_alloc.h"

#include "layer_shared_types.h"
#include "layer_vma_runtime.h"

bool create_staging_copy_for_region(
    VmaRuntime* runtime,
    VkDeviceSize byte_size,
    StagingAllocation* out_staging);

bool create_cpu_upload_staging_for_region(
    VmaRuntime* runtime,
    VkDeviceSize byte_size,
    const void* data,
    StagingAllocation* out_staging);

void release_staging_allocations(
    VmaRuntime* runtime,
    std::vector<StagingAllocation>* allocations);
