#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "layer_shared_types.h"

void track_command_buffer_staging_allocation(
    VkCommandBuffer command_buffer,
    StagingAllocation&& staging);

void take_command_buffer_staging_allocations(
    void* command_buffer_key,
    std::vector<StagingAllocation>* out_allocations);

void track_command_buffer_descriptor_set(
    VkCommandBuffer command_buffer,
    VkDescriptorPool descriptor_pool,
    VkDescriptorSet descriptor_set);

void take_command_buffer_descriptor_sets(
    void* command_buffer_key,
    std::vector<TrackedDescriptorSet>* out_sets);
