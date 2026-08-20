from pathlib import Path

entry = Path("src/layer/layer_entry.cpp")
state_h = Path("src/layer/layer_global_state.h")
state_cpp = Path("src/layer/layer_global_state.cpp")

s = entry.read_text()
h = state_h.read_text()
cpp = state_cpp.read_text()

# ------------------------------------------------------------
# Safety checks
# ------------------------------------------------------------

dispatch_h = Path("src/layer/layer_device_dispatch_types.h").read_text()

for field in (
    "get_device_queue",
    "get_device_queue2",
    "queue_submit",
    "queue_submit2",
    "queue_present_khr",
):
    if field not in dispatch_h:
        raise SystemExit(f"ERROR: missing DeviceDispatch field: {field}")

if "g_queue_dispatch" in h or "g_queue_dispatch" in cpp:
    raise SystemExit(
        "ERROR: g_queue_dispatch already exists. "
        "Refusing duplicate Phase 3A state insertion."
    )

# ------------------------------------------------------------
# Backups
# ------------------------------------------------------------

for path, contents in (
    (entry.with_name("layer_entry.cpp.before-phase3a"), s),
    (state_h.with_name("layer_global_state.h.before-phase3a"), h),
    (state_cpp.with_name("layer_global_state.cpp.before-phase3a"), cpp),
):
    if not path.exists():
        path.write_text(contents)

# ------------------------------------------------------------
# 1. Queue dispatch state
# ------------------------------------------------------------

needle = "extern std::unordered_map<void*, DeviceDispatch> g_device_dispatch;\n"

if needle not in h:
    raise SystemExit(
        "ERROR: g_device_dispatch declaration not found "
        "in layer_global_state.h"
    )

h = h.replace(
    needle,
    needle +
    "extern std::unordered_map<void*, DeviceDispatch> g_queue_dispatch;\n",
    1,
)

needle = "std::unordered_map<void*, DeviceDispatch> g_device_dispatch;\n"

if needle not in cpp:
    raise SystemExit(
        "ERROR: g_device_dispatch definition not found "
        "in layer_global_state.cpp"
    )

cpp = cpp.replace(
    needle,
    needle +
    "std::unordered_map<void*, DeviceDispatch> g_queue_dispatch;\n",
    1,
)

# Explicit queue -> owning-device map.
needle = "extern std::unordered_map<void*, DeviceDispatch> g_queue_dispatch;\n"

h = h.replace(
    needle,
    needle +
    "extern std::unordered_map<void*, void*> g_queue_to_device;\n",
    1,
)

needle = "std::unordered_map<void*, DeviceDispatch> g_queue_dispatch;\n"

cpp = cpp.replace(
    needle,
    needle +
    "std::unordered_map<void*, void*> g_queue_to_device;\n",
    1,
)

# ------------------------------------------------------------
# 2. Load queue/present dispatch functions during device creation
# ------------------------------------------------------------

dispatch_needle = '''    device_dispatch.unmap_memory = reinterpret_cast<PFN_vkUnmapMemory>(
        next_gdpa(*pDevice, "vkUnmapMemory"));
'''

dispatch_replacement = dispatch_needle + '''    // Phase 3A queue/presentation dispatch.
    device_dispatch.get_device_queue =
        reinterpret_cast<PFN_vkGetDeviceQueue>(
            next_gdpa(*pDevice, "vkGetDeviceQueue"));
    device_dispatch.get_device_queue2 =
        reinterpret_cast<PFN_vkGetDeviceQueue2>(
            next_gdpa(*pDevice, "vkGetDeviceQueue2"));
    device_dispatch.queue_submit =
        reinterpret_cast<PFN_vkQueueSubmit>(
            next_gdpa(*pDevice, "vkQueueSubmit"));
    device_dispatch.queue_submit2 =
        reinterpret_cast<PFN_vkQueueSubmit2>(
            next_gdpa(*pDevice, "vkQueueSubmit2"));
#ifdef VK_KHR_swapchain
    device_dispatch.queue_present_khr =
        reinterpret_cast<PFN_vkQueuePresentKHR>(
            next_gdpa(*pDevice, "vkQueuePresentKHR"));
#endif
'''

if "device_dispatch.get_device_queue =" not in s:
    if dispatch_needle not in s:
        raise SystemExit(
            "ERROR: vkUnmapMemory dispatch insertion point not found"
        )
    s = s.replace(dispatch_needle, dispatch_replacement, 1)

# ------------------------------------------------------------
# 3. Queue wrappers
# ------------------------------------------------------------

marker = '''extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
'''

wrappers = r'''
extern "C" VKAPI_ATTR void VKAPI_CALL layer_GetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));

        if (it == g_device_dispatch.end()) {
            if (pQueue) {
                *pQueue = VK_NULL_HANDLE;
            }
            EXYNOS_LOGE(
                "Phase 3A: vkGetDeviceQueue device dispatch not found.");
            return;
        }

        dispatch = it->second;
    }

    if (!dispatch.get_device_queue) {
        if (pQueue) {
            *pQueue = VK_NULL_HANDLE;
        }
        EXYNOS_LOGE(
            "Phase 3A: underlying vkGetDeviceQueue unavailable.");
        return;
    }

    dispatch.get_device_queue(
        device,
        queueFamilyIndex,
        queueIndex,
        pQueue);

    if (pQueue && *pQueue != VK_NULL_HANDLE) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        void* queue_key = dispatch_key(*pQueue);
        void* device_key = dispatch_key(device);

        g_queue_dispatch[queue_key] = dispatch;
        g_queue_to_device[queue_key] = device_key;

        EXYNOS_LOGI(
            "Phase 3A: queue acquired family=%u index=%u.",
            queueFamilyIndex,
            queueIndex);
    }
}


extern "C" VKAPI_ATTR void VKAPI_CALL layer_GetDeviceQueue2(
    VkDevice device,
    const VkDeviceQueueInfo2* pQueueInfo,
    VkQueue* pQueue) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));

        if (it == g_device_dispatch.end()) {
            if (pQueue) {
                *pQueue = VK_NULL_HANDLE;
            }
            EXYNOS_LOGE(
                "Phase 3A: vkGetDeviceQueue2 device dispatch not found.");
            return;
        }

        dispatch = it->second;
    }

    if (!dispatch.get_device_queue2) {
        if (pQueue) {
            *pQueue = VK_NULL_HANDLE;
        }
        EXYNOS_LOGE(
            "Phase 3A: underlying vkGetDeviceQueue2 unavailable.");
        return;
    }

    dispatch.get_device_queue2(
        device,
        pQueueInfo,
        pQueue);

    if (pQueue &&
        *pQueue != VK_NULL_HANDLE &&
        pQueueInfo) {

        std::lock_guard<std::shared_mutex> guard(g_lock);
        void* queue_key = dispatch_key(*pQueue);
        void* device_key = dispatch_key(device);

        g_queue_dispatch[queue_key] = dispatch;
        g_queue_to_device[queue_key] = device_key;

        EXYNOS_LOGI(
            "Phase 3A: queue2 acquired family=%u index=%u.",
            pQueueInfo->queueFamilyIndex,
            pQueueInfo->queueIndex);
    }
}


extern "C" VKAPI_ATTR VkResult VKAPI_CALL layer_QueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence fence) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_queue_dispatch.find(dispatch_key(queue));

        if (it == g_queue_dispatch.end()) {
            EXYNOS_LOGE(
                "Phase 3A: vkQueueSubmit queue dispatch not found.");
            return VK_ERROR_DEVICE_LOST;
        }

        dispatch = it->second;
    }

    if (!dispatch.queue_submit) {
        EXYNOS_LOGE(
            "Phase 3A: underlying vkQueueSubmit unavailable.");
        return VK_ERROR_DEVICE_LOST;
    }

    EXYNOS_LOGI(
        "Phase 3A: vkQueueSubmit intercepted submitCount=%u.",
        submitCount);

    return dispatch.queue_submit(
        queue,
        submitCount,
        pSubmits,
        fence);
}


extern "C" VKAPI_ATTR VkResult VKAPI_CALL layer_QueueSubmit2(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo2* pSubmits,
    VkFence fence) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_queue_dispatch.find(dispatch_key(queue));

        if (it == g_queue_dispatch.end()) {
            EXYNOS_LOGE(
                "Phase 3A: vkQueueSubmit2 queue dispatch not found.");
            return VK_ERROR_DEVICE_LOST;
        }

        dispatch = it->second;
    }

    if (!dispatch.queue_submit2) {
        EXYNOS_LOGE(
            "Phase 3A: underlying vkQueueSubmit2 unavailable.");
        return VK_ERROR_DEVICE_LOST;
    }

    EXYNOS_LOGI(
        "Phase 3A: vkQueueSubmit2 intercepted submitCount=%u.",
        submitCount);

    return dispatch.queue_submit2(
        queue,
        submitCount,
        pSubmits,
        fence);
}


#ifdef VK_KHR_swapchain

extern "C" VKAPI_ATTR VkResult VKAPI_CALL layer_QueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo) {

    DeviceDispatch dispatch{};

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_queue_dispatch.find(dispatch_key(queue));

        if (it == g_queue_dispatch.end()) {
            EXYNOS_LOGE(
                "Phase 3A: vkQueuePresentKHR queue dispatch not found.");
            return VK_ERROR_DEVICE_LOST;
        }

        dispatch = it->second;
    }

    if (!dispatch.queue_present_khr) {
        EXYNOS_LOGE(
            "Phase 3A: underlying vkQueuePresentKHR unavailable.");
        return VK_ERROR_DEVICE_LOST;
    }

    const uint32_t waitCount =
        pPresentInfo ? pPresentInfo->waitSemaphoreCount : 0;

    const uint32_t swapchainCount =
        pPresentInfo ? pPresentInfo->swapchainCount : 0;

    EXYNOS_LOGI(
        "Phase 3A: vkQueuePresentKHR intercepted "
        "waitSemaphores=%u swapchains=%u.",
        waitCount,
        swapchainCount);

    return dispatch.queue_present_khr(
        queue,
        pPresentInfo);
}

#endif

'''

if "Phase 3A: vkQueueSubmit intercepted" not in s:
    if marker not in s:
        raise SystemExit(
            "ERROR: vkGetDeviceProcAddr marker not found"
        )
    s = s.replace(marker, wrappers + marker, 1)

# ------------------------------------------------------------
# 4. vkGetDeviceProcAddr interception
# ------------------------------------------------------------

gdpa_needle = '''    if (!dispatch.get_device_proc_addr) {
        return nullptr;
    }
    return dispatch.get_device_proc_addr(device, pName);
}
'''

gdpa_replacement = '''    if (!dispatch.get_device_proc_addr) {
        return nullptr;
    }

    // Phase 3A: transparent queue/presentation interception.
    // No LSFG scheduling is performed yet.

    if (std::strcmp(pName, "vkGetDeviceQueue") == 0 &&
        dispatch.get_device_queue) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_GetDeviceQueue);
    }

    if (std::strcmp(pName, "vkGetDeviceQueue2") == 0 &&
        dispatch.get_device_queue2) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_GetDeviceQueue2);
    }

    if (std::strcmp(pName, "vkQueueSubmit") == 0 &&
        dispatch.queue_submit) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_QueueSubmit);
    }

    if (std::strcmp(pName, "vkQueueSubmit2") == 0 &&
        dispatch.queue_submit2) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_QueueSubmit2);
    }

#ifdef VK_KHR_swapchain
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0 &&
        dispatch.queue_present_khr) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            layer_QueuePresentKHR);
    }
#endif

    return dispatch.get_device_proc_addr(device, pName);
}
'''

if 'std::strcmp(pName, "vkQueueSubmit") == 0' not in s:
    if gdpa_needle not in s:
        raise SystemExit(
            "ERROR: final vkGetDeviceProcAddr return block not found"
        )
    s = s.replace(gdpa_needle, gdpa_replacement, 1)

# ------------------------------------------------------------
# 5. Queue cleanup during device destruction
# ------------------------------------------------------------

destroy_needle = '''        dispatch = it->second;
        g_device_dispatch.erase(it);
'''

destroy_replacement = '''        dispatch = it->second;
        g_device_dispatch.erase(it);

        // Phase 3A: remove queue mappings owned by this device.
        for (auto queue_it = g_queue_to_device.begin();
             queue_it != g_queue_to_device.end();) {

            if (queue_it->second == device_key) {
                g_queue_dispatch.erase(queue_it->first);
                queue_it = g_queue_to_device.erase(queue_it);
            } else {
                ++queue_it;
            }
        }
'''

if "g_queue_to_device.begin()" not in s:
    if destroy_needle not in s:
        raise SystemExit(
            "ERROR: layer_DestroyDevice dispatch erase point not found"
        )
    s = s.replace(destroy_needle, destroy_replacement, 1)

# ------------------------------------------------------------
# Write
# ------------------------------------------------------------

state_h.write_text(h)
state_cpp.write_text(cpp)
entry.write_text(s)

print("[OK] Added g_queue_dispatch.")
print("[OK] Added g_queue_to_device.")
print("[OK] Added queue/present dispatch loading.")
print("[OK] Added vkGetDeviceQueue wrapper.")
print("[OK] Added vkGetDeviceQueue2 wrapper.")
print("[OK] Added vkQueueSubmit wrapper.")
print("[OK] Added vkQueueSubmit2 wrapper.")
print("[OK] Added vkQueuePresentKHR wrapper.")
print("[OK] Added vkGetDeviceProcAddr interception.")
print("[OK] Added per-device queue cleanup.")
print("[DONE] Phase 3A applied.")
