#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

int main(void)
{
    printf("=== XCLIPSE 940 ROBUSTNESS2 PROBE ===\n");

    void *lib = dlopen("/system/lib64/libvulkan.so",
                       RTLD_NOW | RTLD_LOCAL);

    if (!lib) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }

    PFN_vkGetInstanceProcAddr getProc =
        (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance)dlsym(lib, "vkCreateInstance");

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LSFG-Robustness-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-Robustness-Probe",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_2
    };

    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app
    };

    VkInstance instance = VK_NULL_HANDLE;

    VkResult r = createInstance(&ci, NULL, &instance);

    printf("vkCreateInstance: %d\n", r);

    if (r != VK_SUCCESS)
        return 2;

    PFN_vkEnumeratePhysicalDevices enumerate =
        (PFN_vkEnumeratePhysicalDevices)
        getProc(instance, "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceFeatures2 getFeatures2 =
        (PFN_vkGetPhysicalDeviceFeatures2)
        getProc(instance, "vkGetPhysicalDeviceFeatures2");

    PFN_vkGetPhysicalDeviceProperties getProperties =
        (PFN_vkGetPhysicalDeviceProperties)
        getProc(instance, "vkGetPhysicalDeviceProperties");

    uint32_t count = 0;
    enumerate(instance, &count, NULL);

    VkPhysicalDevice devices[4];

    if (count > 4)
        count = 4;

    enumerate(instance, &count, devices);

    for (uint32_t i = 0; i < count; i++) {

        VkPhysicalDeviceProperties props;

        getProperties(devices[i], &props);

        printf("\nGPU: %s\n", props.deviceName);

        VkPhysicalDeviceRobustness2FeaturesEXT robustness2 = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT
        };

        VkPhysicalDeviceFeatures2 features2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &robustness2
        };

        getFeatures2(devices[i], &features2);

        printf("robustBufferAccess2 : %s\n",
               robustness2.robustBufferAccess2 ? "YES" : "NO");

        printf("robustImageAccess2  : %s\n",
               robustness2.robustImageAccess2 ? "YES" : "NO");

        printf("nullDescriptor      : %s\n",
               robustness2.nullDescriptor ? "YES" : "NO");
    }

    return 0;
}
