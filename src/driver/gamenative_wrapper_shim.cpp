#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <dlfcn.h>

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

struct ShimRuntime {
    void* system_vulkan = nullptr;
    void* exynos_layer = nullptr;

    PFN_vkGetInstanceProcAddr system_gipa = nullptr;
    PFN_vkGetDeviceProcAddr system_gdpa = nullptr;
    PFN_vkCreateInstance system_create_instance = nullptr;
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

void initialize_runtime() {
#if defined(__LP64__)
    constexpr const char* kSystemVulkan = "/system/lib64/libvulkan.so";
#else
    constexpr const char* kSystemVulkan = "/system/lib/libvulkan.so";
#endif
    constexpr const char* kLayerLibrary = "libVkLayer_VortekXclipse.so";

    g_runtime.system_vulkan = dlopen(kSystemVulkan, RTLD_NOW | RTLD_LOCAL);
    if (!g_runtime.system_vulkan) {
        return;
    }

    g_runtime.system_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(g_runtime.system_vulkan, "vkGetInstanceProcAddr"));
    g_runtime.system_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(g_runtime.system_vulkan, "vkGetDeviceProcAddr"));
    g_runtime.system_create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        dlsym(g_runtime.system_vulkan, "vkCreateInstance"));
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
        !g_runtime.system_create_instance ||
        !g_runtime.system_enumerate_instance_extensions) {
        return;
    }

    const std::string layer_path = join_driver_path(
        std::getenv("ADRENOTOOLS_DRIVER_PATH"), kLayerLibrary);
    g_runtime.exynos_layer = dlopen(layer_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!g_runtime.exynos_layer) {
        return;
    }

    g_runtime.layer_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(g_runtime.exynos_layer, "vkGetInstanceProcAddr"));
    g_runtime.layer_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(g_runtime.exynos_layer, "vkGetDeviceProcAddr"));
    if (!g_runtime.layer_gipa || !g_runtime.layer_gdpa) {
        return;
    }

    g_runtime.layer_create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        g_runtime.layer_gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    g_runtime.layer_create_device = reinterpret_cast<PFN_vkCreateDevice>(
        g_runtime.layer_gipa(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!g_runtime.layer_create_instance || !g_runtime.layer_create_device) {
        return;
    }

    g_runtime.ready = true;
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
    layer_link.pfnNextGetInstanceProcAddr = g_runtime.system_gipa;
    layer_link.pfnNextGetPhysicalDeviceProcAddr = nullptr;

    VkLayerInstanceCreateInfo loader_info{};
    loader_info.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
    loader_info.pNext = pCreateInfo->pNext;
    loader_info.function = VK_LAYER_LINK_INFO;
    loader_info.u.pLayerInfo = &layer_link;

    VkInstanceCreateInfo create_info = *pCreateInfo;
    create_info.pNext = &loader_info;
    return g_runtime.layer_create_instance(&create_info, pAllocator, pInstance);
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
    layer_link.pfnNextGetInstanceProcAddr = g_runtime.system_gipa;
    layer_link.pfnNextGetDeviceProcAddr = g_runtime.system_gdpa;

    VkLayerDeviceCreateInfo loader_info{};
    loader_info.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
    loader_info.pNext = pCreateInfo->pNext;
    loader_info.function = VK_LAYER_LINK_INFO;
    loader_info.u.pLayerInfo = &layer_link;

    VkDeviceCreateInfo create_info = *pCreateInfo;
    create_info.pNext = &loader_info;
    return g_runtime.layer_create_device(
        physicalDevice, &create_info, pAllocator, pDevice);
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
    // This library is being consumed as GameNative's custom Vulkan driver, not
    // registered as another implicit layer. Forward driver/global extension
    // discovery to Android's real Vulkan loader.
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
    if (!pName) {
        return nullptr;
    }
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    }
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }
    if (std::strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    }
    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    }
    if (std::strcmp(pName, "vkEnumerateInstanceVersion") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceVersion);
    }
    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    }
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

    // Once an instance exists, let the ExynosTools layer resolve its own
    // interception table first. Unknown functions naturally fall through to
    // the downstream Android Vulkan dispatch recorded by the layer.
    if (instance != VK_NULL_HANDLE) {
        if (PFN_vkVoidFunction proc = g_runtime.layer_gipa(instance, pName)) {
            return proc;
        }
    }
    return g_runtime.system_gipa(instance, pName);
}

extern "C" EXYNOS_DRIVER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!pName || !ensure_runtime()) {
        return nullptr;
    }
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }
    if (device != VK_NULL_HANDLE) {
        if (PFN_vkVoidFunction proc = g_runtime.layer_gdpa(device, pName)) {
            return proc;
        }
    }
    return g_runtime.system_gdpa(device, pName);
}
