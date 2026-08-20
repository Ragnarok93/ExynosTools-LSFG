#!/usr/bin/env python3
from pathlib import Path
import shutil
import re

ROOT = Path(__file__).resolve().parent

FILES = {
    "dispatch": ROOT / "src/layer/layer_device_dispatch_types.h",
    "entry": ROOT / "src/layer/layer_entry.cpp",
    "state_h": ROOT / "src/layer/layer_global_state.h",
    "state_cpp": ROOT / "src/layer/layer_global_state.cpp",
}

def backup(path):
    backup = path.with_name(path.name + ".phase3b-before")
    if not backup.exists():
        shutil.copy2(path, backup)
        print(f"backup: {backup}")

def read(path):
    return path.read_text()

def write(path, text):
    path.write_text(text)

def replace_once(text, old, new, label):
    count = text.count(old)
    if count == 0:
        raise RuntimeError(f"{label}: insertion point not found")
    if count > 1:
        raise RuntimeError(f"{label}: insertion point occurs {count} times")
    return text.replace(old, new, 1)

for path in FILES.values():
    if not path.exists():
        raise RuntimeError(f"Missing file: {path}")
    backup(path)

# ---------------------------------------------------------------------------
# 1. Device dispatch: add swapchain functions.
# ---------------------------------------------------------------------------

path = FILES["dispatch"]
text = read(path)

if "PFN_vkCreateSwapchainKHR create_swapchain_khr" not in text:
    marker = """    PFN_vkQueuePresentKHR queue_present_khr = nullptr;
"""

    insertion = """    PFN_vkQueuePresentKHR queue_present_khr = nullptr;

#ifdef VK_KHR_swapchain
    // Phase 3B swapchain lifecycle interception.
    PFN_vkCreateSwapchainKHR create_swapchain_khr = nullptr;
    PFN_vkDestroySwapchainKHR destroy_swapchain_khr = nullptr;
    PFN_vkGetSwapchainImagesKHR get_swapchain_images_khr = nullptr;
    PFN_vkAcquireNextImageKHR acquire_next_image_khr = nullptr;
#ifdef VK_KHR_device_group
    PFN_vkAcquireNextImage2KHR acquire_next_image2_khr = nullptr;
#endif
#endif
"""

    text = replace_once(
        text,
        marker,
        insertion,
        "DeviceDispatch swapchain insertion",
    )
    write(path, text)
    print("updated:", path)
else:
    print("already updated:", path)

# ---------------------------------------------------------------------------
# 2. Global state: swapchain tracking.
#
# Keep this deliberately lightweight. Phase 3B only needs ownership/lifecycle
# tracking; actual frame interpolation is a later phase.
# ---------------------------------------------------------------------------

state_h = FILES["state_h"]
text = read(state_h)

if "g_swapchain_to_device" not in text:
    marker = """extern std::unordered_map<void*, void*> g_queue_to_device;
"""

    insertion = """extern std::unordered_map<void*, void*> g_queue_to_device;

// Phase 3B swapchain ownership/state tracking.
extern std::unordered_map<void*, void*> g_swapchain_to_device;
extern std::unordered_map<void*, VkSwapchainKHR> g_swapchain_handles;
"""

    text = replace_once(
        text,
        marker,
        insertion,
        "global swapchain declarations",
    )
    write(state_h, text)
    print("updated:", state_h)
else:
    print("already updated:", state_h)

state_cpp = FILES["state_cpp"]
text = read(state_cpp)

if "g_swapchain_to_device" not in text:
    marker = """std::unordered_map<void*, void*> g_queue_to_device;
"""

    insertion = """std::unordered_map<void*, void*> g_queue_to_device;

// Phase 3B swapchain ownership/state tracking.
std::unordered_map<void*, void*> g_swapchain_to_device;
std::unordered_map<void*, VkSwapchainKHR> g_swapchain_handles;
"""

    text = replace_once(
        text,
        marker,
        insertion,
        "global swapchain definitions",
    )
    write(state_cpp, text)
    print("updated:", state_cpp)
else:
    print("already updated:", state_cpp)

# ---------------------------------------------------------------------------
# 3. Device creation: load the swapchain dispatch functions.
# ---------------------------------------------------------------------------

entry = FILES["entry"]
text = read(entry)

if "device_dispatch.create_swapchain_khr" not in text:
    marker = """#ifdef VK_KHR_swapchain
    device_dispatch.queue_present_khr =
        reinterpret_cast<PFN_vkQueuePresentKHR>(
            next_gdpa(*pDevice, "vkQueuePresentKHR"));
#endif
"""

    insertion = """#ifdef VK_KHR_swapchain
    device_dispatch.queue_present_khr =
        reinterpret_cast<PFN_vkQueuePresentKHR>(
            next_gdpa(*pDevice, "vkQueuePresentKHR"));

    // Phase 3B swapchain lifecycle dispatch.
    device_dispatch.create_swapchain_khr =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(
            next_gdpa(*pDevice, "vkCreateSwapchainKHR"));
    device_dispatch.destroy_swapchain_khr =
        reinterpret_cast<PFN_vkDestroySwapchainKHR>(
            next_gdpa(*pDevice, "vkDestroySwapchainKHR"));
    device_dispatch.get_swapchain_images_khr =
        reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
            next_gdpa(*pDevice, "vkGetSwapchainImagesKHR"));
    device_dispatch.acquire_next_image_khr =
        reinterpret_cast<PFN_vkAcquireNextImageKHR>(
            next_gdpa(*pDevice, "vkAcquireNextImageKHR"));
#ifdef VK_KHR_device_group
    device_dispatch.acquire_next_image2_khr =
        reinterpret_cast<PFN_vkAcquireNextImage2KHR>(
            next_gdpa(*pDevice, "vkAcquireNextImage2KHR"));
#endif
#endif
"""

    text = replace_once(
        text,
        marker,
        insertion,
        "device swapchain dispatch initialization",
    )
    write(entry, text)
    print("updated:", entry)
else:
    print("already updated:", entry)

# ---------------------------------------------------------------------------
# 4. Insert swapchain wrappers immediately before vkGetDeviceProcAddr.
# ---------------------------------------------------------------------------

text = read(entry)

if "layer_CreateSwapchainKHR" not in text:
    marker = """extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
"""

    wrappers = r'''#ifdef VK_KHR_swapchain

extern "C" VKAPI_ATTR VkResult VKAPI_CALL layer_CreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));

        if (it == g_device_dispatch.end()) {
            EXYNOS_LOGW(
                "Phase 3B: vkCreateSwapchainKHR device dispatch not found.");
            return VK_ERROR_DEVICE_LOST;
        }

        dispatch = it->second;
    }

    if (!dispatch.create_swapchain_khr) {
        EXYNOS_LOGW(
            "Phase 3B: underlying vkCreateSwapchainKHR unavailable.");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkResult result = dispatch.create_swapchain_khr(
        device,
        pCreateInfo,
        pAllocator,
        pSwapchain);

    if (result == VK_SUCCESS &&
        pSwapchain &&
        *pSwapchain != VK_NULL_HANDLE) {

        const void* swapchain_key = dispatch_key(*pSwapchain);
        const void* device_key = dispatch_key(device);

        {
            std::lock_guard<std::shared_mutex> guard(g_lock);
            g_swapchain_to_device[
                const_cast<void*>(swapchain_key)] =
                const_cast<void*>(device_key);
            g_swapchain_handles[
                const_cast<void*>(swapchain_key)] =
                *pSwapchain;
        }

        const uint32_t imageCount =
            pCreateInfo ? pCreateInfo->minImageCount : 0;

        EXYNOS_LOGI(
            "Phase 3B: vkCreateSwapchainKHR intercepted "
            "minImageCount=%u format=%d extent=%ux%u.",
            imageCount,
            pCreateInfo
                ? static_cast<int>(pCreateInfo->imageFormat)
                : 0,
            pCreateInfo ? pCreateInfo->imageExtent.width : 0,
            pCreateInfo ? pCreateInfo->imageExtent.height : 0);
    }

    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL layer_DestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));

        if (it == g_device_dispatch.end()) {
            EXYNOS_LOGW(
                "Phase 3B: vkDestroySwapchainKHR device dispatch not found.");
            return;
        }

        dispatch = it->second;
    }

    if (!dispatch.destroy_swapchain_khr) {
        EXYNOS_LOGW(
            "Phase 3B: underlying vkDestroySwapchainKHR unavailable.");
        return;
    }

    EXYNOS_LOGI(
        "Phase 3B: vkDestroySwapchainKHR intercepted.");

    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        void* swapchain_key = dispatch_key(swapchain);
        g_swapchain_to_device.erase(swapchain_key);
        g_swapchain_handles.erase(swapchain_key);
    }

    dispatch.destroy_swapchain_khr(
        device,
        swapchain,
        pAllocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL layer_GetSwapchainImagesKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint32_t* pSwapchainImageCount,
    VkImage* pSwapchainImages) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));

        if (it == g_device_dispatch.end()) {
            EXYNOS_LOGW(
                "Phase 3B: vkGetSwapchainImagesKHR device dispatch not found.");
            return VK_ERROR_DEVICE_LOST;
        }

        dispatch = it->second;
    }

    if (!dispatch.get_swapchain_images_khr) {
        EXYNOS_LOGW(
            "Phase 3B: underlying vkGetSwapchainImagesKHR unavailable.");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkResult result = dispatch.get_swapchain_images_khr(
        device,
        swapchain,
        pSwapchainImageCount,
        pSwapchainImages);

    if (result == VK_SUCCESS && pSwapchainImageCount) {
        EXYNOS_LOGI(
            "Phase 3B: vkGetSwapchainImagesKHR intercepted "
            "imageCount=%u.",
            *pSwapchainImageCount);
    }

    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL layer_AcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t* pImageIndex) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));

        if (it == g_device_dispatch.end()) {
            EXYNOS_LOGW(
                "Phase 3B: vkAcquireNextImageKHR device dispatch not found.");
            return VK_ERROR_DEVICE_LOST;
        }

        dispatch = it->second;
    }

    if (!dispatch.acquire_next_image_khr) {
        EXYNOS_LOGW(
            "Phase 3B: underlying vkAcquireNextImageKHR unavailable.");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkResult result = dispatch.acquire_next_image_khr(
        device,
        swapchain,
        timeout,
        semaphore,
        fence,
        pImageIndex);

    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        EXYNOS_LOGI(
            "Phase 3B: vkAcquireNextImageKHR intercepted "
            "result=%d imageIndex=%u.",
            static_cast<int>(result),
            pImageIndex ? *pImageIndex : 0);
    }

    return result;
}

#ifdef VK_KHR_device_group

extern "C" VKAPI_ATTR VkResult VKAPI_CALL layer_AcquireNextImage2KHR(
    VkDevice device,
    const VkAcquireNextImageInfoKHR* pAcquireInfo,
    uint32_t* pImageIndex) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));

        if (it == g_device_dispatch.end()) {
            EXYNOS_LOGW(
                "Phase 3B: vkAcquireNextImage2KHR device dispatch not found.");
            return VK_ERROR_DEVICE_LOST;
        }

        dispatch = it->second;
    }

    if (!dispatch.acquire_next_image2_khr) {
        EXYNOS_LOGW(
            "Phase 3B: underlying vkAcquireNextImage2KHR unavailable.");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkResult result = dispatch.acquire_next_image2_khr(
        device,
        pAcquireInfo,
        pImageIndex);

    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        EXYNOS_LOGI(
            "Phase 3B: vkAcquireNextImage2KHR intercepted "
            "result=%d imageIndex=%u.",
            static_cast<int>(result),
            pImageIndex ? *pImageIndex : 0);
    }

    return result;
}

#endif
#endif

'''

    text = replace_once(
        text,
        marker,
        wrappers + marker,
        "swapchain wrappers",
    )
    write(entry, text)
    print("updated:", entry)
else:
    print("already updated:", entry)

# ---------------------------------------------------------------------------
# 5. vkGetDeviceProcAddr interception.
# ---------------------------------------------------------------------------

text = read(entry)

replacements = {
    '"vkCreateSwapchainKHR"': "layer_CreateSwapchainKHR",
    '"vkDestroySwapchainKHR"': "layer_DestroySwapchainKHR",
    '"vkGetSwapchainImagesKHR"': "layer_GetSwapchainImagesKHR",
    '"vkAcquireNextImageKHR"': "layer_AcquireNextImageKHR",
    '"vkAcquireNextImage2KHR"': "layer_AcquireNextImage2KHR",
}

# Insert before the first normal copy-path dispatch handling after the
# existing queue-present interception.
if "Phase 3B: vkCreateSwapchainKHR proc-addr intercepted." not in text:
    marker = """#ifdef VK_KHR_swapchain
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0 &&
        should_intercept_queue_path()) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_QueuePresentKHR);
    }
#endif
"""

    insertion = """#ifdef VK_KHR_swapchain
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0 &&
        should_intercept_queue_path()) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_QueuePresentKHR);
    }

    if (std::strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        EXYNOS_LOGI(
            "Phase 3B: vkCreateSwapchainKHR proc-addr intercepted.");
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_CreateSwapchainKHR);
    }

    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0) {
        EXYNOS_LOGI(
            "Phase 3B: vkDestroySwapchainKHR proc-addr intercepted.");
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_DestroySwapchainKHR);
    }

    if (std::strcmp(pName, "vkGetSwapchainImagesKHR") == 0) {
        EXYNOS_LOGI(
            "Phase 3B: vkGetSwapchainImagesKHR proc-addr intercepted.");
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_GetSwapchainImagesKHR);
    }

    if (std::strcmp(pName, "vkAcquireNextImageKHR") == 0) {
        EXYNOS_LOGI(
            "Phase 3B: vkAcquireNextImageKHR proc-addr intercepted.");
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_AcquireNextImageKHR);
    }

#ifdef VK_KHR_device_group
    if (std::strcmp(pName, "vkAcquireNextImage2KHR") == 0) {
        EXYNOS_LOGI(
            "Phase 3B: vkAcquireNextImage2KHR proc-addr intercepted.");
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_AcquireNextImage2KHR);
    }
#endif
#endif
"""

    if marker not in text:
        raise RuntimeError(
            "vkGetDeviceProcAddr queue-present interception block not found"
        )

    text = replace_once(
        text,
        marker,
        insertion,
        "vkGetDeviceProcAddr Phase 3B interception",
    )
    write(entry, text)
    print("updated:", entry)
else:
    print("already updated:", entry)

# ---------------------------------------------------------------------------
# 6. Device destruction: remove swapchains belonging to destroyed device.
# ---------------------------------------------------------------------------

text = read(entry)

if "Phase 3B: remove swapchain mappings owned by this device." not in text:
    marker = """        // Phase 3A: remove queue mappings owned by this device.
        for (auto queue_it = g_queue_to_device.begin();
"""

    insertion = """        // Phase 3B: remove swapchain mappings owned by this device.
        for (auto swapchain_it = g_swapchain_to_device.begin();
             swapchain_it != g_swapchain_to_device.end();) {

            if (swapchain_it->second == device_key) {
                g_swapchain_handles.erase(swapchain_it->first);
                swapchain_it = g_swapchain_to_device.erase(swapchain_it);
            } else {
                ++swapchain_it;
            }
        }

        // Phase 3A: remove queue mappings owned by this device.
        for (auto queue_it = g_queue_to_device.begin();
"""

    text = replace_once(
        text,
        marker,
        insertion,
        "device destruction swapchain cleanup",
    )
    write(entry, text)
    print("updated:", entry)
else:
    print("already updated:", entry)

print()
print("Phase 3B source changes applied.")
print("Next: build and run the compile checks.")
