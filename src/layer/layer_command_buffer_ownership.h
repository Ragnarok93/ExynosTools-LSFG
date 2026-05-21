#pragma once

#include <vector>

#include <vulkan/vulkan.h>

void track_command_pool_device(VkCommandPool command_pool, VkDevice device);

void track_allocated_command_buffers(
    VkDevice device,
    VkCommandPool command_pool,
    uint32_t command_buffer_count,
    const VkCommandBuffer* command_buffers);

void collect_command_buffers_for_device(
    void* device_key,
    std::vector<void*>* out_command_buffer_keys);

void collect_command_buffers_for_pool(
    void* command_pool_key,
    std::vector<void*>* out_command_buffer_keys);

void list_command_buffers_for_pool(
    void* command_pool_key,
    std::vector<void*>* out_command_buffer_keys);

void erase_command_pool_tracking(void* command_pool_key);

void erase_command_buffer_tracking(void* command_buffer_key);

bool get_command_buffer_device_mapping(
    VkCommandBuffer command_buffer,
    void** out_device_key,
    VkDevice* out_device);
