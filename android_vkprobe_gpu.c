#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

int main(void)
{
    printf("=== ANDROID VULKAN GPU PROBE ===\n");
    fflush(stdout);

    void *lib = dlopen("/system/lib64/libvulkan.so",
                       RTLD_NOW | RTLD_LOCAL);

    if (!lib) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }

    PFN_vkGetInstanceProcAddr getProc =
        (PFN_vkGetInstanceProcAddr)dlsym(
            lib, "vkGetInstanceProcAddr");

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance)dlsym(
            lib, "vkCreateInstance");

    if (!getProc || !createInstance) {
        printf("Required loader symbols missing\n");
        return 2;
    }

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "LSFG-GPU-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-GPU-Probe",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_0
    };

    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &app,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL
    };

    VkInstance instance = VK_NULL_HANDLE;

    VkResult r = createInstance(&ci, NULL, &instance);

    printf("vkCreateInstance: %d\n", r);
    printf("instance: %p\n", (void *)instance);
    fflush(stdout);

    if (r != VK_SUCCESS)
        return 3;

    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)
        getProc(instance, "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceProperties getProperties =
        (PFN_vkGetPhysicalDeviceProperties)
        getProc(instance, "vkGetPhysicalDeviceProperties");

    if (!enumeratePhysicalDevices || !getProperties) {
        printf("Physical-device entry points unavailable\n");
        return 4;
    }

    uint32_t count = 0;

    r = enumeratePhysicalDevices(instance, &count, NULL);

    printf("vkEnumeratePhysicalDevices: %d\n", r);
    printf("GPU count: %u\n", count);
    fflush(stdout);

    if (r != VK_SUCCESS || count == 0)
        return 5;

    VkPhysicalDevice devices[8];

    if (count > 8)
        count = 8;

    r = enumeratePhysicalDevices(instance, &count, devices);

    printf("GPU enumeration: %d\n", r);
    fflush(stdout);

    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;

        getProperties(devices[i], &props);

        printf("\nGPU %u\n", i);
        printf("  deviceName: %s\n", props.deviceName);
        printf("  vendorID: 0x%04x\n", props.vendorID);
        printf("  deviceID: 0x%04x\n", props.deviceID);
        printf("  deviceType: %u\n", props.deviceType);
        printf("  driverVersion: 0x%08x\n", props.driverVersion);
        printf("  apiVersion: %u.%u.%u\n",
               VK_VERSION_MAJOR(props.apiVersion),
               VK_VERSION_MINOR(props.apiVersion),
               VK_VERSION_PATCH(props.apiVersion));
        fflush(stdout);
    }

    return 0;
}
