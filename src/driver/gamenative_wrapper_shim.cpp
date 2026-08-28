#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <dlfcn.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#if defined(__GNUC__)
#define EXYNOS_DRIVER_EXPORT __attribute__((visibility("default")))
#else
#define EXYNOS_DRIVER_EXPORT
#endif

namespace {

constexpr const char* kLogTag = "ExynosToolsShim";

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

struct ShimRuntime {
    void* system_vulkan = nullptr;
    void* exynos_layer = nullptr;

    PFN_vkGetInstanceProcAddr system_gipa = nullptr;
    PFN_vkGetDeviceProcAddr system_gdpa = nullptr;
    PFN_vkCreateInstance system_create_instance = nullptr;
    PFN_vkCreateDevice system_create_device = nullptr;
    PFN_vkEnumerateInstanceVersion system_enumerate_instance_version = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties system_enumerate_instance_extensions = nullptr;
    PFN_vkEnumerateInstanceLayerProperties system_enumerate_instance_layers = nullptr;

    PFN_vkGetInstanceProcAddr layer_gipa = nullptr;
    PFN_vkGetDeviceProcAddr layer_gdpa = nullptr;
    PFN_vkCreateInstance layer_create_instance = nullptr;
    PFN_vkCreateDevice layer_create_device = nullptr;

    bool ready = false;
};

ShimRuntime g_runtime;
std::once_flag g_runtime_once;

std::string join_driver_path(const char* directory, const char* filename) {
    if (!directory || directory[0] == '\0') {
        return filename;
    }
    std::string path(directory);
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    path += filename;
    return path;
}

const void* strip_synthetic_instance_link(const void* pNext) {
    if (!pNext) return nullptr;
    const auto* base = reinterpret_cast<const VkBaseInStructure*>(pNext);
    if (base->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO) {
        const auto* loader = reinterpret_cast<const VkLayerInstanceCreateInfo*>(pNext);
        if (loader->function == VK_LAYER_LINK_INFO) {
            return loader->pNext;
        }
    }
    return pNext;
}

const void* strip_synthetic_device_link(const void* pNext) {
    if (!pNext) return nullptr;
    const auto* base = reinterpret_cast<const VkBaseInStructure*>(pNext);
    if (base->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
        const auto* loader = reinterpret_cast<const VkLayerDeviceCreateInfo*>(pNext);
        if (loader->function == VK_LAYER_LINK_INFO) {
            return loader->pNext;
        }
    }
    return pNext;
}

VKAPI_ATTR VkResult VKAPI_CALL downstream_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    if (!pCreateInfo || !g_runtime.system_create_instance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkInstanceCreateInfo clean = *pCreateInfo;
    clean.pNext = strip_synthetic_instance_link(pCreateInfo->pNext);
    return g_runtime.system_create_instance(&clean, pAllocator, pInstance);
}

VKAPI_ATTR VkResult VKAPI_CALL downstream_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    if (!pCreateInfo || !g_runtime.system_create_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkDeviceCreateInfo clean = *pCreateInfo;
    clean.pNext = strip_synthetic_device_link(pCreateInfo->pNext);
    return g_runtime.system_create_device(physicalDevice, &clean, pAllocator, pDevice);
}

PFN_vkVoidFunction VKAPI_CALL downstream_gipa(VkInstance instance, const char* pName);
PFN_vkVoidFunction VKAPI_CALL downstream_gdpa(VkDevice device, const char* pName);

PFN_vkVoidFunction VKAPI_CALL downstream_gipa(VkInstance instance, const char* pName) {
    if (!pName) return nullptr;
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(downstream_gipa);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(downstream_gdpa);
    if (std::strcmp(pName, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(downstream_CreateInstance);
    if (std::strcmp(pName, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(downstream_CreateDevice);
    return g_runtime.system_gipa ? g_runtime.system_gipa(instance, pName) : nullptr;
}

PFN_vkVoidFunction VKAPI_CALL downstream_gdpa(VkDevice device, const char* pName) {
    if (!pName) return nullptr;
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(downstream_gdpa);
    return g_runtime.system_gdpa ? g_runtime.system_gdpa(device, pName) : nullptr;
}

void initialize_runtime() {
#if defined(__LP64__)
    constexpr const char* kSystemVulkan = "/system/lib64/libvulkan.so";
#else
    constexpr const char* kSystemVulkan = "/system/lib/libvulkan.so";
#endif
    constexpr const char* kLayerLibrary = "libVkLayer_VortekXclipse.so";

    shim_log("initializing stock-Wrapper driver shim");
    g_runtime.system_vulkan = dlopen(kSystemVulkan, RTLD_NOW | RTLD_LOCAL);
    if (!g_runtime.system_vulkan) {
        shim_log_error(dlerror());
        return;
    }

    g_runtime.system_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(g_runtime.system_vulkan, "vkGetInstanceProcAddr"));
    g_runtime.system_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(g_runtime.system_vulkan, "vkGetDeviceProcAddr"));
    g_runtime.system_create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        dlsym(g_runtime.system_vulkan, "vkCreateInstance"));
    g_runtime.system_create_device = reinterpret_cast<PFN_vkCreateDevice>(
        dlsym(g_runtime.system_vulkan, "vkCreateDevice"));
    g_runtime.system_enumerate_instance_version =
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            dlsym(g_runtime.system_vulkan, "vkEnumerateInstanceVersion"));
    g_runtime.system_enumerate_instance_extensions =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            dlsym(g_runtime.system_vulkan, "vkEnumerateInstanceExtensionProperties"));
    g_runtime.system_enumerate_instance_layers =
        reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
            dlsym(g_runtime.system_vulkan, "vkEnumerateInstanceLayerProperties"));

    if (!g_runtime.system_gipa || !g_runtime.system_gdpa ||
        !g_runtime.system_create_instance || !g_runtime.system_create_device ||
        !g_runtime.system_enumerate_instance_extensions) {
        shim_log_error("system Vulkan exports incomplete");
        return;
    }

    const std::string layer_path = join_driver_path(
        std::getenv("ADRENOTOOLS_DRIVER_PATH"), kLayerLibrary);
    g_runtime.exynos_layer = dlopen(layer_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!g_runtime.exynos_layer) {
        shim_log_error(dlerror());
        return;
    }

    g_runtime.layer_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(g_runtime.exynos_layer, "vkGetInstanceProcAddr"));
    g_runtime.layer_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(g_runtime.exynos_layer, "vkGetDeviceProcAddr"));
    if (!g_runtime.layer_gipa || !g_runtime.layer_gdpa) {
        shim_log_error("ExynosTools layer entrypoints incomplete");
        return;
    }

    g_runtime.layer_create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        g_runtime.layer_gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    g_runtime.layer_create_device = reinterpret_cast<PFN_vkCreateDevice>(
        g_runtime.layer_gipa(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!g_runtime.layer_create_instance || !g_runtime.layer_create_device) {
        shim_log_error("ExynosTools layer create entrypoints unavailable");
        return;
    }

    g_runtime.ready = true;
    shim_log("stock-Wrapper driver shim ready");
}

bool ensure_runtime() {
    std::call_once(g_runtime_once, initialize_runtime);
    return g_runtime.ready;
}

PFN_vkVoidFunction shim_global_proc(const char* pName);

}  // namespace

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    if (!pCreateInfo || !pInstance || !ensure_runtime()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkLayerInstanceLink layer_link{};
    layer_link.pNext = nullptr;
    layer_link.pfnNextGetInstanceProcAddr = downstream_gipa;
    layer_link.pfnNextGetPhysicalDeviceProcAddr = nullptr;

    VkLayerInstanceCreateInfo loader_info{};
    loader_info.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
    loader_info.pNext = pCreateInfo->pNext;
    loader_info.function = VK_LAYER_LINK_INFO;
    loader_info.u.pLayerInfo = &layer_link;

    VkInstanceCreateInfo create_info = *pCreateInfo;
    create_info.pNext = &loader_info;
    VkResult result = g_runtime.layer_create_instance(&create_info, pAllocator, pInstance);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "vkCreateInstance -> %d", result);
#endif
    return result;
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    if (!pCreateInfo || !pDevice || !ensure_runtime()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkLayerDeviceLink layer_link{};
    layer_link.pNext = nullptr;
    layer_link.pfnNextGetInstanceProcAddr = downstream_gipa;
    layer_link.pfnNextGetDeviceProcAddr = downstream_gdpa;

    VkLayerDeviceCreateInfo loader_info{};
    loader_info.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
    loader_info.pNext = pCreateInfo->pNext;
    loader_info.function = VK_LAYER_LINK_INFO;
    loader_info.u.pLayerInfo = &layer_link;

    VkDeviceCreateInfo create_info = *pCreateInfo;
    create_info.pNext = &loader_info;
    VkResult result = g_runtime.layer_create_device(
        physicalDevice, &create_info, pAllocator, pDevice);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "vkCreateDevice -> %d", result);
#endif
    return result;
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(
    uint32_t* pApiVersion) {
    if (!pApiVersion || !ensure_runtime()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_runtime.system_enumerate_instance_version) {
        *pApiVersion = VK_API_VERSION_1_0;
        return VK_SUCCESS;
    }
    return g_runtime.system_enumerate_instance_version(pApiVersion);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    if (!pPropertyCount || !ensure_runtime()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return g_runtime.system_enumerate_instance_extensions(
        pLayerName, pPropertyCount, pProperties);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    if (!pPropertyCount || !ensure_runtime()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_runtime.system_enumerate_instance_layers) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    return g_runtime.system_enumerate_instance_layers(pPropertyCount, pProperties);
}

namespace {

PFN_vkVoidFunction shim_global_proc(const char* pName) {
    if (!pName) return nullptr;
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (std::strcmp(pName, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    if (std::strcmp(pName, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    if (std::strcmp(pName, "vkEnumerateInstanceVersion") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceVersion);
    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    return nullptr;
}

}  // namespace

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (PFN_vkVoidFunction global = shim_global_proc(pName)) {
        return global;
    }
    if (!ensure_runtime()) {
        return nullptr;
    }
    if (instance != VK_NULL_HANDLE) {
        if (PFN_vkVoidFunction proc = g_runtime.layer_gipa(instance, pName)) {
            return proc;
        }
    }
    return g_runtime.system_gipa(instance, pName);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!pName || !ensure_runtime()) return nullptr;
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (device != VK_NULL_HANDLE) {
        if (PFN_vkVoidFunction proc = g_runtime.layer_gdpa(device, pName)) {
            return proc;
        }
    }
    return g_runtime.system_gdpa(device, pName);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vkGetInstanceProcAddr(instance, pName);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    return vkGetInstanceProcAddr(instance, pName);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    if (!pSupportedVersion) return VK_ERROR_INITIALIZATION_FAILED;
    *pSupportedVersion = std::min(*pSupportedVersion, 5u);
    return VK_SUCCESS;
}
