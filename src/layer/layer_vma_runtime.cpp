#include "layer_vma_runtime.h"

uint32_t clamp_vma_api_version(uint32_t api_version) {
    if (api_version == 0) {
        return VK_API_VERSION_1_0;
    }

    uint32_t variant = VK_API_VERSION_VARIANT(api_version);
    uint32_t major = VK_API_VERSION_MAJOR(api_version);
    uint32_t minor = VK_API_VERSION_MINOR(api_version);

    if (major == 0) {
        return VK_API_VERSION_1_0;
    }
    if (major > 1) {
        return VK_MAKE_API_VERSION(variant, 1, 3, 0);
    }

    if (minor > 3) {
        minor = 3;
    }
    return VK_MAKE_API_VERSION(variant, 1, minor, 0);
}

bool initialize_vma_runtime(
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkDevice device,
    const InstanceDispatch& instance_dispatch,
    const DeviceDispatch& dispatch,
    VmaRuntime* runtime) {
    if (!runtime) {
        return false;
    }
    if (runtime->initialized) {
        return runtime->allocator != VK_NULL_HANDLE;
    }
    runtime->initialized = true;

    if (instance == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
        return false;
    }
    if (!dispatch.get_device_proc_addr || !instance_dispatch.get_instance_proc_addr) {
        return false;
    }

    runtime->vulkan_functions = {};
    runtime->vulkan_functions.vkGetInstanceProcAddr = instance_dispatch.get_instance_proc_addr;
    runtime->vulkan_functions.vkGetDeviceProcAddr = dispatch.get_device_proc_addr;

    uint32_t vma_api_version = VK_API_VERSION_1_0;
    if (instance_dispatch.get_physical_device_properties) {
        VkPhysicalDeviceProperties props{};
        instance_dispatch.get_physical_device_properties(physical_device, &props);
        vma_api_version = clamp_vma_api_version(props.apiVersion);
        runtime->min_storage_buffer_offset_alignment =
            std::max<VkDeviceSize>(16, props.limits.minStorageBufferOffsetAlignment);
    }

    VmaAllocatorCreateInfo allocator_ci{};
    allocator_ci.physicalDevice = physical_device;
    allocator_ci.device = device;
    allocator_ci.instance = instance;
    allocator_ci.pVulkanFunctions = &runtime->vulkan_functions;
    allocator_ci.vulkanApiVersion = vma_api_version;

    VkResult vma_result = vmaCreateAllocator(&allocator_ci, &runtime->allocator);
    if (vma_result != VK_SUCCESS || runtime->allocator == VK_NULL_HANDLE) {
        runtime->allocator = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void destroy_vma_runtime(VmaRuntime* runtime) {
    if (!runtime) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(runtime->staging_mutex);
        for (const StagingChunk& chunk : runtime->staging_chunks) {
            if (runtime->allocator != VK_NULL_HANDLE &&
                chunk.buffer != VK_NULL_HANDLE &&
                chunk.allocation != VK_NULL_HANDLE) {
                vmaDestroyBuffer(runtime->allocator, chunk.buffer, chunk.allocation);
            }
        }
        runtime->staging_chunks.clear();
    }
    if (runtime->allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(runtime->allocator);
        runtime->allocator = VK_NULL_HANDLE;
    }
    runtime->initialized = false;
}
