#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

int main(void)
{
    printf("=== XCLIPSE 940 QUEUE / TIMESTAMP PROBE ===\n");

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
        .pApplicationName = "LSFG-Queue-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-Queue-Probe",
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
        (PFN_vkEnumeratePhysicalDevices)getProc(
            instance, "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceProperties getProperties =
        (PFN_vkGetPhysicalDeviceProperties)getProc(
            instance, "vkGetPhysicalDeviceProperties");

    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueues =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)
        getProc(instance, "vkGetPhysicalDeviceQueueFamilyProperties");

    uint32_t count = 0;
    enumerate(instance, &count, NULL);

    VkPhysicalDevice devices[4];
    if (count > 4)
        count = 4;

    enumerate(instance, &count, devices);

    for (uint32_t d = 0; d < count; d++) {
        VkPhysicalDeviceProperties props;
        getProperties(devices[d], &props);

        printf("\nGPU: %s\n", props.deviceName);
        printf("timestampPeriod: %.3f ns\n", props.limits.timestampPeriod);
        printf("timestampComputeAndGraphics: %s\n",
               props.limits.timestampComputeAndGraphics ? "YES" : "NO");
        printf("maxComputeWorkGroupCount: %u %u %u\n",
               props.limits.maxComputeWorkGroupCount[0],
               props.limits.maxComputeWorkGroupCount[1],
               props.limits.maxComputeWorkGroupCount[2]);

        uint32_t qcount = 0;
        getQueues(devices[d], &qcount, NULL);

        printf("Queue families: %u\n", qcount);

        VkQueueFamilyProperties q[qcount];
        getQueues(devices[d], &qcount, q);

        for (uint32_t i = 0; i < qcount; i++) {
            printf("\nQueue family %u\n", i);
            printf("  count: %u\n", q[i].queueCount);
            printf("  flags: 0x%08x\n", q[i].queueFlags);
            printf("  GRAPHICS: %s\n",
                   (q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? "YES" : "NO");
            printf("  COMPUTE : %s\n",
                   (q[i].queueFlags & VK_QUEUE_COMPUTE_BIT) ? "YES" : "NO");
            printf("  TRANSFER: %s\n",
                   (q[i].queueFlags & VK_QUEUE_TRANSFER_BIT) ? "YES" : "NO");
            printf("  SPARSE  : %s\n",
                   (q[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) ? "YES" : "NO");
        }
    }

    return 0;
}
