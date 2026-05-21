#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "layer_device_dispatch_types.h"
#include "layer_pipeline_selection.h"
#include "layer_shared_types.h"
#include "layer_vma_runtime.h"

struct ComputeRuntimeConfig {
    uint32_t push_constant_size = 0;
    uint32_t initial_descriptor_pool_capacity = 0;
    uint32_t descriptor_pool_growth_cap = 0;
    uint32_t preferred_subgroup_size = 0;
    bool supports_wave32 = false;
    bool supports_wave64 = false;
    std::string pipeline_cache_path;
    bool descriptor_buffer_supported = false;
    VkDeviceSize descriptor_buffer_offset_alignment = 16;
    size_t storage_image_descriptor_size = 0;
    size_t storage_buffer_descriptor_size = 0;
    size_t combined_image_sampler_descriptor_size = 0;
};

bool allocate_decode_descriptor_set(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set);

bool prepare_decode_descriptor_set(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaRuntime* vma_runtime,
    VkImageView storage_view,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize range,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set);

bool prepare_special_copy_descriptor_set(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaRuntime* vma_runtime,
    VkImageView dst_view,
    VkImageView src_view,
    VkSampler sampler,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set);

bool initialize_compute_runtime(
    VkDevice device,
    const DeviceDispatch& dispatch,
    const ComputeRuntimeConfig& config,
    ComputeRuntime* runtime);

void destroy_compute_runtime(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaRuntime* vma_runtime);

void release_descriptor_sets(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    std::vector<TrackedDescriptorSet>* descriptor_sets);
