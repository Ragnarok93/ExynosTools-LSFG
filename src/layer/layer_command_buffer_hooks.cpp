#include "layer_command_buffer_hooks.h"

#include <vector>

#include "layer_command_buffer_ownership.h"
#include "layer_command_buffer_resources.h"
#include "layer_dispatch_key.h"
#include "layer_staging_allocations.h"
#include "layer_telemetry.h"

namespace {

bool lookup_device_dispatch(
    std::shared_mutex& lock,
    std::unordered_map<void*, DeviceDispatch>& device_dispatch,
    VkDevice device,
    DeviceDispatch* out_dispatch) {
    if (!out_dispatch) {
        return false;
    }

    std::shared_lock<std::shared_mutex> guard(lock);
    auto it = device_dispatch.find(dispatch_key(device));
    if (it == device_dispatch.end()) {
        return false;
    }

    *out_dispatch = it->second;
    return true;
}

bool gather_command_buffer_cleanup_state(
    const CommandBufferHookContext& context,
    VkDevice device,
    DeviceDispatch* out_dispatch,
    std::shared_ptr<VmaRuntime>* out_vma_runtime,
    std::shared_ptr<ComputeRuntime>* out_compute_runtime) {
    if (!out_dispatch || !out_vma_runtime || !out_compute_runtime) {
        return false;
    }

    out_vma_runtime->reset();
    out_compute_runtime->reset();

    std::shared_lock<std::shared_mutex> guard(context.lock);
    auto it = context.device_dispatch.find(dispatch_key(device));
    if (it == context.device_dispatch.end()) {
        return false;
    }

    *out_dispatch = it->second;

    auto vma_it = context.vma_runtime.find(dispatch_key(device));
    if (vma_it != context.vma_runtime.end() && vma_it->second) {
        *out_vma_runtime = vma_it->second;
    }

    auto compute_it = context.compute_runtime.find(dispatch_key(device));
    if (compute_it != context.compute_runtime.end() && compute_it->second) {
        *out_compute_runtime = compute_it->second;
    }

    return true;
}

}  // namespace

bool try_get_any_device_dispatch(
    const CommandBufferDispatchContext& context,
    DeviceDispatch* out_dispatch) {
    if (!out_dispatch) {
        return false;
    }

    std::shared_lock<std::shared_mutex> guard(context.lock);
    if (context.device_dispatch.size() != 1) {
        return false;
    }

    *out_dispatch = context.device_dispatch.begin()->second;
    return true;
}

bool try_get_command_buffer_dispatch(
    const CommandBufferDispatchContext& context,
    VkCommandBuffer command_buffer,
    const char* api_name,
    DeviceDispatch* out_dispatch,
    VkDevice* out_device) {
    if (!out_dispatch || !out_device) {
        return false;
    }

    *out_device = VK_NULL_HANDLE;

    void* device_key = nullptr;
    if (!get_command_buffer_device_mapping(command_buffer, &device_key, out_device)) {
        if (context.warn_missing_map) {
            context.warn_missing_map(api_name);
        }
        return false;
    }

    std::shared_lock<std::shared_mutex> guard(context.lock);
    auto it_dev = context.device_dispatch.find(device_key);
    if (it_dev == context.device_dispatch.end()) {
        if (context.warn_missing_map) {
            context.warn_missing_map(api_name);
        }
        return false;
    }

    *out_dispatch = it_dev->second;
    return true;
}

bool resolve_command_buffer_dispatch(
    const CommandBufferDispatchContext& context,
    VkCommandBuffer command_buffer,
    const char* api_name,
    DeviceDispatch* out_dispatch,
    VkDevice* out_device) {
    if (try_get_command_buffer_dispatch(
            context, command_buffer, api_name, out_dispatch, out_device)) {
        return true;
    }

    const LayerSettingsSnapshot settings = context.snapshot_layer_settings
        ? context.snapshot_layer_settings()
        : LayerSettingsSnapshot{};

    if (settings.strict_dispatch || settings.drop_on_missing_commandbuffer_map) {
        if (context.warn_dispatch_drop) {
            context.warn_dispatch_drop(api_name);
        }
        return false;
    }

    if (!try_get_any_device_dispatch(context, out_dispatch)) {
        return false;
    }

    if (out_device) {
        *out_device = VK_NULL_HANDLE;
    }

    if (context.warn_dispatch_fallback) {
        context.warn_dispatch_fallback(api_name);
    }
    return true;
}

VkResult handle_create_command_pool(
    const CommandBufferHookContext& context,
    VkDevice device,
    const VkCommandPoolCreateInfo* create_info,
    const VkAllocationCallbacks* allocator,
    VkCommandPool* command_pool) {
    DeviceDispatch dispatch{};
    if (!lookup_device_dispatch(context.lock, context.device_dispatch, device, &dispatch)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!dispatch.create_command_pool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.create_command_pool(device, create_info, allocator, command_pool);
    if (result == VK_SUCCESS && command_pool && *command_pool != VK_NULL_HANDLE) {
        track_command_pool_device(*command_pool, device);
    }
    return result;
}

void handle_destroy_command_pool(
    const CommandBufferHookContext& context,
    VkDevice device,
    VkCommandPool command_pool,
    const VkAllocationCallbacks* allocator) {
    DeviceDispatch dispatch{};
    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    std::shared_ptr<VmaRuntime> vma_runtime;
    std::shared_ptr<ComputeRuntime> compute_runtime;
    if (!gather_command_buffer_cleanup_state(
            context, device, &dispatch, &vma_runtime, &compute_runtime)) {
        return;
    }

    std::vector<void*> command_buffer_keys_to_release;
    collect_command_buffers_for_pool(dispatch_key(command_pool), &command_buffer_keys_to_release);
    erase_command_pool_tracking(dispatch_key(command_pool));
    for (void* command_buffer_key : command_buffer_keys_to_release) {
        take_command_buffer_staging_allocations(
            command_buffer_key, &staging_allocations_to_release);
        take_command_buffer_descriptor_sets(
            command_buffer_key, &descriptor_sets_to_release);
    }

    release_staging_allocations(vma_runtime.get(), &staging_allocations_to_release);
    release_descriptor_sets(
        device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
    collect_gpu_microbenchmarks(device, dispatch, false);
    if (dispatch.destroy_command_pool) {
        dispatch.destroy_command_pool(device, command_pool, allocator);
    }
}

VkResult handle_reset_command_pool(
    const CommandBufferHookContext& context,
    VkDevice device,
    VkCommandPool command_pool,
    VkCommandPoolResetFlags flags) {
    DeviceDispatch dispatch{};
    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    std::shared_ptr<VmaRuntime> vma_runtime;
    std::shared_ptr<ComputeRuntime> compute_runtime;
    if (!gather_command_buffer_cleanup_state(
            context, device, &dispatch, &vma_runtime, &compute_runtime)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<void*> command_buffer_keys_to_release;
    list_command_buffers_for_pool(dispatch_key(command_pool), &command_buffer_keys_to_release);
    for (void* command_buffer_key : command_buffer_keys_to_release) {
        take_command_buffer_staging_allocations(
            command_buffer_key, &staging_allocations_to_release);
        take_command_buffer_descriptor_sets(
            command_buffer_key, &descriptor_sets_to_release);
    }

    if (!dispatch.reset_command_pool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.reset_command_pool(device, command_pool, flags);
    if (result == VK_SUCCESS) {
        release_staging_allocations(vma_runtime.get(), &staging_allocations_to_release);
        release_descriptor_sets(
            device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
        collect_gpu_microbenchmarks(device, dispatch, false);
    }
    return result;
}

VkResult handle_begin_command_buffer(
    const CommandBufferDispatchContext& dispatch_context,
    std::function<void(VkDevice, VkCommandBuffer, const DeviceDispatch&)> release_resources,
    VkCommandBuffer command_buffer,
    const VkCommandBufferBeginInfo* begin_info) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!try_get_command_buffer_dispatch(
            dispatch_context,
            command_buffer,
            "vkBeginCommandBuffer",
            &dispatch,
            &device)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!dispatch.begin_command_buffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.begin_command_buffer(command_buffer, begin_info);
    if (result == VK_SUCCESS && release_resources) {
        release_resources(device, command_buffer, dispatch);
    }
    return result;
}

VkResult handle_reset_command_buffer(
    const CommandBufferDispatchContext& dispatch_context,
    std::function<void(VkDevice, VkCommandBuffer, const DeviceDispatch&)> release_resources,
    VkCommandBuffer command_buffer,
    VkCommandBufferResetFlags flags) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!try_get_command_buffer_dispatch(
            dispatch_context,
            command_buffer,
            "vkResetCommandBuffer",
            &dispatch,
            &device)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!dispatch.reset_command_buffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.reset_command_buffer(command_buffer, flags);
    if (result == VK_SUCCESS && release_resources) {
        release_resources(device, command_buffer, dispatch);
    }
    return result;
}

VkResult handle_allocate_command_buffers(
    const CommandBufferHookContext& context,
    VkDevice device,
    const VkCommandBufferAllocateInfo* allocate_info,
    VkCommandBuffer* command_buffers) {
    DeviceDispatch dispatch{};
    if (!lookup_device_dispatch(context.lock, context.device_dispatch, device, &dispatch)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!dispatch.allocate_command_buffers) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.allocate_command_buffers(device, allocate_info, command_buffers);
    if (result == VK_SUCCESS && allocate_info && command_buffers) {
        track_allocated_command_buffers(
            device,
            allocate_info->commandPool,
            allocate_info->commandBufferCount,
            command_buffers);
    }
    return result;
}

void handle_free_command_buffers(
    const CommandBufferHookContext& context,
    VkDevice device,
    VkCommandPool command_pool,
    uint32_t command_buffer_count,
    const VkCommandBuffer* command_buffers) {
    DeviceDispatch dispatch{};
    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    std::shared_ptr<VmaRuntime> vma_runtime;
    std::shared_ptr<ComputeRuntime> compute_runtime;
    if (!gather_command_buffer_cleanup_state(
            context, device, &dispatch, &vma_runtime, &compute_runtime)) {
        return;
    }

    if (command_buffers) {
        for (uint32_t i = 0; i < command_buffer_count; ++i) {
            void* cb_key = dispatch_key(command_buffers[i]);
            take_command_buffer_staging_allocations(cb_key, &staging_allocations_to_release);
            take_command_buffer_descriptor_sets(cb_key, &descriptor_sets_to_release);
            erase_command_buffer_tracking(cb_key);
        }
    }

    release_staging_allocations(vma_runtime.get(), &staging_allocations_to_release);
    release_descriptor_sets(
        device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
    collect_gpu_microbenchmarks(device, dispatch, false);
    if (dispatch.free_command_buffers) {
        dispatch.free_command_buffers(
            device, command_pool, command_buffer_count, command_buffers);
    }
}
