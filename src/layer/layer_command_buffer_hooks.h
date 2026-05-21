#pragma once

#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "layer_compute_runtime.h"
#include "layer_device_dispatch_types.h"
#include "layer_settings_types.h"
#include "layer_shared_types.h"
#include "layer_vma_runtime.h"

struct CommandBufferDispatchContext {
    std::shared_mutex& lock;
    std::unordered_map<void*, DeviceDispatch>& device_dispatch;
    std::function<LayerSettingsSnapshot()> snapshot_layer_settings;
    std::function<void(const char* api_name)> warn_missing_map;
    std::function<void(const char* api_name)> warn_dispatch_drop;
    std::function<void(const char* api_name)> warn_dispatch_fallback;
};

struct CommandBufferHookContext {
    std::shared_mutex& lock;
    std::unordered_map<void*, DeviceDispatch>& device_dispatch;
    std::unordered_map<void*, std::shared_ptr<ComputeRuntime>>& compute_runtime;
    std::unordered_map<void*, std::shared_ptr<VmaRuntime>>& vma_runtime;
};

bool try_get_any_device_dispatch(
    const CommandBufferDispatchContext& context,
    DeviceDispatch* out_dispatch);

bool try_get_command_buffer_dispatch(
    const CommandBufferDispatchContext& context,
    VkCommandBuffer command_buffer,
    const char* api_name,
    DeviceDispatch* out_dispatch,
    VkDevice* out_device);

bool resolve_command_buffer_dispatch(
    const CommandBufferDispatchContext& context,
    VkCommandBuffer command_buffer,
    const char* api_name,
    DeviceDispatch* out_dispatch,
    VkDevice* out_device);

VkResult handle_create_command_pool(
    const CommandBufferHookContext& context,
    VkDevice device,
    const VkCommandPoolCreateInfo* create_info,
    const VkAllocationCallbacks* allocator,
    VkCommandPool* command_pool);

void handle_destroy_command_pool(
    const CommandBufferHookContext& context,
    VkDevice device,
    VkCommandPool command_pool,
    const VkAllocationCallbacks* allocator);

VkResult handle_reset_command_pool(
    const CommandBufferHookContext& context,
    VkDevice device,
    VkCommandPool command_pool,
    VkCommandPoolResetFlags flags);

VkResult handle_begin_command_buffer(
    const CommandBufferDispatchContext& dispatch_context,
    std::function<void(VkDevice, VkCommandBuffer, const DeviceDispatch&)> release_resources,
    VkCommandBuffer command_buffer,
    const VkCommandBufferBeginInfo* begin_info);

VkResult handle_reset_command_buffer(
    const CommandBufferDispatchContext& dispatch_context,
    std::function<void(VkDevice, VkCommandBuffer, const DeviceDispatch&)> release_resources,
    VkCommandBuffer command_buffer,
    VkCommandBufferResetFlags flags);

VkResult handle_allocate_command_buffers(
    const CommandBufferHookContext& context,
    VkDevice device,
    const VkCommandBufferAllocateInfo* allocate_info,
    VkCommandBuffer* command_buffers);

void handle_free_command_buffers(
    const CommandBufferHookContext& context,
    VkDevice device,
    VkCommandPool command_pool,
    uint32_t command_buffer_count,
    const VkCommandBuffer* command_buffers);
