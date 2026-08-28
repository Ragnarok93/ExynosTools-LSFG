#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <dlfcn.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#if defined(__GNUC__)
#define EXYNOS_DRIVER_EXPORT __attribute__((visibility("default")))
#else
#define EXYNOS_DRIVER_EXPORT
#endif

// Minimal copies of Android's stable libhardware + hwvulkan ABI. These are
// platform ABI structs, not an attempt to use private Android implementation
// details. GameNative's AdrenoTools hook substitutes this library where Android
// libvulkan expects a vendor Vulkan HAL module (normally vulkan.samsung.so).
struct hw_module_t;
struct hw_device_t;

struct hw_module_methods_t {
    int (*open)(const hw_module_t* module, const char* id, hw_device_t** device);
};

struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char* id;
    const char* name;
    const char* author;
    hw_module_methods_t* methods;
    void* dso;
#if defined(__LP64__)
    uint64_t reserved[32 - 7];
#else
    uint32_t reserved[32 - 7];
#endif
};

struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    hw_module_t* module;
#if defined(__LP64__)
    uint64_t reserved[12];
#else
    uint32_t reserved[12];
#endif
    int (*close)(hw_device_t* device);
};

struct hwvulkan_module_t {
    hw_module_t common;
};

struct hwvulkan_device_t {
    hw_device_t common;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
};

constexpr uint32_t make_tag(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

constexpr uint32_t HARDWARE_MODULE_TAG = make_tag('H', 'W', 'M', 'T');
constexpr uint32_t HARDWARE_DEVICE_TAG = make_tag('H', 'W', 'D', 'T');
constexpr uint16_t HARDWARE_HAL_API_VERSION = 0x0100;
constexpr uint16_t HWVULKAN_MODULE_API_VERSION_0_1 = 0x0001;
constexpr uint32_t HWVULKAN_DEVICE_API_VERSION_0_1 = 0x00010000;
constexpr const char* HWVULKAN_HARDWARE_MODULE_ID = "vulkan";
constexpr const char* HWVULKAN_DEVICE_0 = "vk0";

#define HAL_MODULE_INFO_SYM HMI
extern "C" EXYNOS_DRIVER_EXPORT hwvulkan_module_t HAL_MODULE_INFO_SYM;

namespace {

constexpr const char* kLogTag = "ExynosToolsShim";
constexpr const char* kLayerLibrary = "libVkLayer_VortekXclipse.so";
constexpr const char* kVndkSupportLibrary = "libvndksupport.so";
constexpr const char* kSamsungVulkanHal = "vulkan.samsung.so";

using PFN_android_load_sphal_library = void* (*)(const char* name, int flags);

void shim_log(const char* msg) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg ? msg : "(null)");
#else
    (void)msg;
#endif
}

void shim_log_error(const char* msg) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", msg ? msg : "(null)");
#else
    (void)msg;
#endif
}

void shim_log_result(const char* api, VkResult result) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s -> %d", api, result);
#else
    (void)api;
    (void)result;
#endif
}

std::string join_path(const char* directory, const char* filename) {
    if (!directory || directory[0] == '\0') return filename;
    std::string path(directory);
    if (!path.empty() && path.back() != '/') path.push_back('/');
    path += filename;
    return path;
}

struct ShimRuntime {
    void* vndksupport = nullptr;
    void* samsung_hal_so = nullptr;
    hwvulkan_module_t* samsung_module = nullptr;
    hwvulkan_device_t* samsung_device = nullptr;

    void* exynos_layer = nullptr;
    PFN_vkGetInstanceProcAddr layer_gipa = nullptr;
    PFN_vkGetDeviceProcAddr layer_gdpa = nullptr;
    PFN_vkCreateInstance layer_create_instance = nullptr;
    PFN_vkCreateDevice layer_create_device = nullptr;

    PFN_vkGetDeviceProcAddr samsung_gdpa = nullptr;
    VkInstance last_instance = VK_NULL_HANDLE;
    bool ready = false;
};

ShimRuntime g_runtime;
std::once_flag g_runtime_once;

const void* strip_synthetic_instance_link(const void* pNext) {
    if (!pNext) return nullptr;
    const auto* base = reinterpret_cast<const VkBaseInStructure*>(pNext);
    if (base->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO) {
        const auto* loader = reinterpret_cast<const VkLayerInstanceCreateInfo*>(pNext);
        if (loader->function == VK_LAYER_LINK_INFO) return loader->pNext;
    }
    return pNext;
}

const void* strip_synthetic_device_link(const void* pNext) {
    if (!pNext) return nullptr;
    const auto* base = reinterpret_cast<const VkBaseInStructure*>(pNext);
    if (base->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
        const auto* loader = reinterpret_cast<const VkLayerDeviceCreateInfo*>(pNext);
        if (loader->function == VK_LAYER_LINK_INFO) return loader->pNext;
    }
    return pNext;
}

void log_requested_instance_extensions(const VkInstanceCreateInfo* create_info) {
#ifdef __ANDROID__
    if (!create_info) return;
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "HAL vkCreateInstance requested %u extensions",
        create_info->enabledExtensionCount);
    for (uint32_t i = 0; i < create_info->enabledExtensionCount; ++i) {
        const char* name = create_info->ppEnabledExtensionNames
            ? create_info->ppEnabledExtensionNames[i]
            : nullptr;
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "  instance-ext[%u]=%s",
            i,
            name ? name : "(null)");
    }
#else
    (void)create_info;
#endif
}

bool open_real_samsung_hal() {
    // Resolve android_load_sphal_library from libvndksupport itself. GameNative's
    // AdrenoTools patch modifies calls made from Android libvulkan; calling the
    // real SP-HAL helper here opens Samsung's built-in vendor module directly.
    g_runtime.vndksupport = dlopen(kVndkSupportLibrary, RTLD_NOW | RTLD_LOCAL);
    PFN_android_load_sphal_library load_sphal = nullptr;
    if (g_runtime.vndksupport) {
        load_sphal = reinterpret_cast<PFN_android_load_sphal_library>(
            dlsym(g_runtime.vndksupport, "android_load_sphal_library"));
    }
    if (!load_sphal) {
        load_sphal = reinterpret_cast<PFN_android_load_sphal_library>(
            dlsym(RTLD_DEFAULT, "android_load_sphal_library"));
    }
    if (!load_sphal) {
        shim_log_error("android_load_sphal_library unavailable");
        return false;
    }

    g_runtime.samsung_hal_so = load_sphal(kSamsungVulkanHal, RTLD_NOW | RTLD_LOCAL);
    if (!g_runtime.samsung_hal_so) {
        shim_log_error("failed to open vulkan.samsung.so through SP-HAL namespace");
        return false;
    }

    g_runtime.samsung_module = reinterpret_cast<hwvulkan_module_t*>(
        dlsym(g_runtime.samsung_hal_so, "HMI"));
    if (!g_runtime.samsung_module) {
        shim_log_error("Samsung Vulkan HAL is missing HMI");
        return false;
    }
    if (!g_runtime.samsung_module->common.id ||
        std::strcmp(g_runtime.samsung_module->common.id, HWVULKAN_HARDWARE_MODULE_ID) != 0 ||
        !g_runtime.samsung_module->common.methods ||
        !g_runtime.samsung_module->common.methods->open) {
        shim_log_error("Samsung Vulkan HAL HMI is incompatible");
        return false;
    }

    g_runtime.samsung_module->common.dso = g_runtime.samsung_hal_so;

    hw_device_t* raw_device = nullptr;
    const int open_result = g_runtime.samsung_module->common.methods->open(
        &g_runtime.samsung_module->common,
        HWVULKAN_DEVICE_0,
        &raw_device);
    if (open_result != 0 || !raw_device) {
#ifdef __ANDROID__
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "Samsung Vulkan HAL open(vk0) failed: %d",
            open_result);
#endif
        return false;
    }

    g_runtime.samsung_device = reinterpret_cast<hwvulkan_device_t*>(raw_device);
    if (!g_runtime.samsung_device->EnumerateInstanceExtensionProperties ||
        !g_runtime.samsung_device->CreateInstance ||
        !g_runtime.samsung_device->GetInstanceProcAddr) {
        shim_log_error("Samsung hwvulkan_device_t callbacks are incomplete");
        return false;
    }

    g_runtime.samsung_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(g_runtime.samsung_hal_so, "vkGetDeviceProcAddr"));
    shim_log("Samsung Vulkan HAL opened");
    return true;
}

bool load_exynos_layer() {
    const char* driver_dir = std::getenv("ADRENOTOOLS_DRIVER_PATH");
    if (!driver_dir || driver_dir[0] == '\0') {
        shim_log_error("ADRENOTOOLS_DRIVER_PATH is missing");
        return false;
    }

    // Critical for the below-loader topology: Android libvulkan installs its
    // own loader data into dispatchable handles after HAL calls return. Tell the
    // ExynosTools layer to key its state by stable handle value rather than the
    // mutable first dispatch pointer. Normal standalone layer operation keeps
    // the standard dispatch-pointer behavior because this variable is absent.
    if (setenv("EXYNOSTOOLS_DRIVER_HOSTED", "1", 1) != 0) {
        shim_log_error("failed to enable EXYNOSTOOLS_DRIVER_HOSTED");
        return false;
    }

    const std::string layer_path = join_path(driver_dir, kLayerLibrary);
    g_runtime.exynos_layer = dlopen(layer_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!g_runtime.exynos_layer) {
        shim_log_error(dlerror());
        return false;
    }

    g_runtime.layer_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(g_runtime.exynos_layer, "vkGetInstanceProcAddr"));
    g_runtime.layer_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(g_runtime.exynos_layer, "vkGetDeviceProcAddr"));
    if (!g_runtime.layer_gipa || !g_runtime.layer_gdpa) {
        shim_log_error("ExynosTools layer GIPA/GDPA unavailable");
        return false;
    }

    g_runtime.layer_create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        g_runtime.layer_gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    g_runtime.layer_create_device = reinterpret_cast<PFN_vkCreateDevice>(
        g_runtime.layer_gipa(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!g_runtime.layer_create_instance || !g_runtime.layer_create_device) {
        shim_log_error("ExynosTools layer create entrypoints unavailable");
        return false;
    }

    shim_log("ExynosTools compatibility layer loaded behind HAL (stable handle dispatch keys)");
    return true;
}

void initialize_runtime() {
    shim_log("r5 initializing Android Vulkan HAL shim");
    if (!open_real_samsung_hal()) return;
    if (!load_exynos_layer()) return;
    g_runtime.ready = true;
    shim_log("r5 Android Vulkan HAL shim ready");
}

bool ensure_runtime() {
    std::call_once(g_runtime_once, initialize_runtime);
    return g_runtime.ready;
}

VKAPI_ATTR VkResult VKAPI_CALL real_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    if (!pCreateInfo || !pInstance || !g_runtime.samsung_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstanceCreateInfo clean = *pCreateInfo;
    clean.pNext = strip_synthetic_instance_link(pCreateInfo->pNext);
    VkResult result = g_runtime.samsung_device->CreateInstance(&clean, pAllocator, pInstance);
    shim_log_result("Samsung HAL vkCreateInstance", result);

    if (result == VK_SUCCESS && *pInstance != VK_NULL_HANDLE) {
        g_runtime.last_instance = *pInstance;
        if (!g_runtime.samsung_gdpa) {
            g_runtime.samsung_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                g_runtime.samsung_device->GetInstanceProcAddr(
                    *pInstance,
                    "vkGetDeviceProcAddr"));
        }
    }
    return result;
}

PFN_vkVoidFunction VKAPI_CALL real_next_gipa(VkInstance instance, const char* pName) {
    if (!pName || !g_runtime.samsung_device) return nullptr;
    if (std::strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(real_CreateInstance);
    }
    return g_runtime.samsung_device->GetInstanceProcAddr(instance, pName);
}

PFN_vkVoidFunction VKAPI_CALL real_next_gdpa(VkDevice device, const char* pName) {
    if (!pName) return nullptr;
    if (g_runtime.samsung_gdpa) {
        return g_runtime.samsung_gdpa(device, pName);
    }
    if (g_runtime.samsung_device && g_runtime.last_instance != VK_NULL_HANDLE) {
        return g_runtime.samsung_device->GetInstanceProcAddr(
            g_runtime.last_instance,
            pName);
    }
    return nullptr;
}

VKAPI_ATTR VkResult VKAPI_CALL hal_EnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    if (!ensure_runtime() || !g_runtime.samsung_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return g_runtime.samsung_device->EnumerateInstanceExtensionProperties(
        pLayerName,
        pPropertyCount,
        pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL hal_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    if (!pCreateInfo || !pInstance || !ensure_runtime()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    log_requested_instance_extensions(pCreateInfo);

    VkLayerInstanceLink layer_link{};
    layer_link.pNext = nullptr;
    layer_link.pfnNextGetInstanceProcAddr = real_next_gipa;
    layer_link.pfnNextGetPhysicalDeviceProcAddr = nullptr;

    VkLayerInstanceCreateInfo loader_info{};
    loader_info.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
    loader_info.pNext = pCreateInfo->pNext;
    loader_info.function = VK_LAYER_LINK_INFO;
    loader_info.u.pLayerInfo = &layer_link;

    VkInstanceCreateInfo create_info = *pCreateInfo;
    create_info.pNext = &loader_info;
    VkResult result = g_runtime.layer_create_instance(&create_info, pAllocator, pInstance);
    shim_log_result("HAL vkCreateInstance", result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL shim_EnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {
    if (!instance || !pPhysicalDeviceCount || !ensure_runtime() || !g_runtime.layer_gipa) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        g_runtime.layer_gipa(instance, "vkEnumeratePhysicalDevices"));
    if (!enumerate) {
        shim_log_error("layer vkEnumeratePhysicalDevices unavailable");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = enumerate(instance, pPhysicalDeviceCount, pPhysicalDevices);
#ifdef __ANDROID__
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "HAL vkEnumeratePhysicalDevices -> %d count=%u data=%d",
        result,
        pPhysicalDeviceCount ? *pPhysicalDeviceCount : 0,
        pPhysicalDevices ? 1 : 0);
#else
    (void)pPhysicalDevices;
#endif
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL shim_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    if (!pCreateInfo || !pDevice || !ensure_runtime()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkLayerDeviceLink layer_link{};
    layer_link.pNext = nullptr;
    layer_link.pfnNextGetInstanceProcAddr = real_next_gipa;
    layer_link.pfnNextGetDeviceProcAddr = real_next_gdpa;

    VkLayerDeviceCreateInfo loader_info{};
    loader_info.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
    loader_info.pNext = pCreateInfo->pNext;
    loader_info.function = VK_LAYER_LINK_INFO;
    loader_info.u.pLayerInfo = &layer_link;

    VkDeviceCreateInfo create_info = *pCreateInfo;
    create_info.pNext = &loader_info;
    VkResult result = g_runtime.layer_create_device(
        physicalDevice,
        &create_info,
        pAllocator,
        pDevice);
    shim_log_result("HAL vkCreateDevice", result);
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL shim_GetDeviceProcAddr(
    VkDevice device,
    const char* pName) {
    if (!pName || !ensure_runtime()) return nullptr;
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(shim_GetDeviceProcAddr);
    }
    if (device != VK_NULL_HANDLE && g_runtime.layer_gdpa) {
        if (PFN_vkVoidFunction proc = g_runtime.layer_gdpa(device, pName)) return proc;
    }
    return real_next_gdpa(device, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hal_GetInstanceProcAddr(
    VkInstance instance,
    const char* pName) {
    if (!pName || !ensure_runtime()) return nullptr;

    // These functions need shim-owned behavior rather than the layer's raw
    // entrypoints. vkCreateDevice injects the synthetic device link, while
    // vkEnumeratePhysicalDevices provides a boundary diagnostic for the first
    // post-instance operation Android libvulkan performs.
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(hal_GetInstanceProcAddr);
    }
    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(hal_EnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(hal_CreateInstance);
    }
    if (std::strcmp(pName, "vkEnumeratePhysicalDevices") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(shim_EnumeratePhysicalDevices);
    }
    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(shim_CreateDevice);
    }
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(shim_GetDeviceProcAddr);
    }

    if (instance != VK_NULL_HANDLE && g_runtime.layer_gipa) {
        if (PFN_vkVoidFunction proc = g_runtime.layer_gipa(instance, pName)) return proc;
    }
    return real_next_gipa(instance, pName);
}

int hal_CloseDevice(hw_device_t*) {
    return 0;
}

int hal_OpenDevice(const hw_module_t*, const char* id, hw_device_t** device);

hw_module_methods_t g_hal_module_methods = {
    hal_OpenDevice,
};

hwvulkan_device_t g_hal_device = {
    {
        HARDWARE_DEVICE_TAG,
        HWVULKAN_DEVICE_API_VERSION_0_1,
        &HAL_MODULE_INFO_SYM.common,
        {0},
        hal_CloseDevice,
    },
    hal_EnumerateInstanceExtensionProperties,
    hal_CreateInstance,
    hal_GetInstanceProcAddr,
};

int hal_OpenDevice(const hw_module_t*, const char* id, hw_device_t** device) {
    if (!id || std::strcmp(id, HWVULKAN_DEVICE_0) != 0 || !device) return -ENOENT;
    shim_log("HAL HMI open(vk0)");
    if (!ensure_runtime()) return -EINVAL;
    *device = &g_hal_device.common;
    return 0;
}

}  // namespace

extern "C" EXYNOS_DRIVER_EXPORT hwvulkan_module_t HAL_MODULE_INFO_SYM = {
    {
        HARDWARE_MODULE_TAG,
        HWVULKAN_MODULE_API_VERSION_0_1,
        HARDWARE_HAL_API_VERSION,
        HWVULKAN_HARDWARE_MODULE_ID,
        "ExynosTools Xclipse Vulkan HAL",
        "Vortek / ExynosTools",
        &g_hal_module_methods,
        nullptr,
        {0},
    },
};

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    return hal_EnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    return hal_CreateInstance(pCreateInfo, pAllocator, pInstance);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return hal_GetInstanceProcAddr(instance, pName);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    return shim_CreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    return shim_GetDeviceProcAddr(device, pName);
}
