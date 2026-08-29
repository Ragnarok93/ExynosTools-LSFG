#pragma once

#include <cstdint>
#include <cstdlib>
#include <type_traits>

inline bool driver_hosted_dispatch_keys() {
    // A conventional Vulkan layer lives above the loader, where dispatchable
    // handles keep the loader/layer dispatch pointer used as the standard key.
    //
    // The GameNative custom-driver integration is different: ExynosTools is
    // manually hosted *below* Android libvulkan as the replacement hwvulkan
    // HAL. Android libvulkan writes its own loader data into the first pointer
    // of VkInstance/VkPhysicalDevice/VkDevice (and other dispatchable handles)
    // after the downstream call returns. Keying state by that mutable first
    // pointer would orphan the layer's maps immediately after vkCreateInstance.
    static const bool enabled = []() {
        const char* value = std::getenv("EXYNOSTOOLS_DRIVER_HOSTED");
        return value && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

template <typename T>
inline void* stable_handle_key(T handle) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<void*>(handle);
    } else {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
    }
}

template <typename T>
inline void* normal_layer_dispatch_key(T handle) {
    if (!handle) {
        return nullptr;
    }
    return *reinterpret_cast<void**>(handle);
}

template <typename T>
inline void* dispatch_key(T handle) {
    if (!handle) {
        return nullptr;
    }
    return driver_hosted_dispatch_keys()
        ? stable_handle_key(handle)
        : normal_layer_dispatch_key(handle);
}
