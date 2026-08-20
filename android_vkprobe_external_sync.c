#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

static void check_semaphore(
    VkPhysicalDevice pd,
    PFN_vkGetPhysicalDeviceExternalSemaphoreProperties fn,
    VkExternalSemaphoreHandleTypeFlagBits type,
    const char *name)
{
    VkPhysicalDeviceExternalSemaphoreInfo info = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
        .pNext = NULL,
        .handleType = type
    };

    VkExternalSemaphoreProperties props = {
        .sType =
            VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
        .pNext = NULL
    };

    fn(pd, &info, &props);

    printf("\n%s\n", name);
    printf("  exportFromImportedHandleTypes: 0x%08x\n",
           props.exportFromImportedHandleTypes);
    printf("  compatibleHandleTypes        : 0x%08x\n",
           props.compatibleHandleTypes);
    printf("  externalSemaphoreFeatures    : 0x%08x\n",
           props.externalSemaphoreFeatures);

    printf("  EXPORTABLE: %s\n",
           (props.externalSemaphoreFeatures &
            VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) ?
            "YES" : "NO");

    printf("  IMPORTABLE: %s\n",
           (props.externalSemaphoreFeatures &
            VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) ?
            "YES" : "NO");
}

int main(void)
{
    printf("=== XCLIPSE 940 EXTERNAL SYNCHRONIZATION PROBE ===\n");

    void *lib = dlopen(
        "/system/lib64/libvulkan.so",
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

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LSFG-Sync-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-Sync-Probe",
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

    PFN_vkGetPhysicalDeviceExternalSemaphoreProperties getExternal =
        (PFN_vkGetPhysicalDeviceExternalSemaphoreProperties)getProc(
            instance,
            "vkGetPhysicalDeviceExternalSemaphoreProperties");

    if (!enumerate || !getExternal) {
        printf("Required functions unavailable\n");
        return 3;
    }

    uint32_t count = 0;
    enumerate(instance, &count, NULL);

    VkPhysicalDevice devices[4];

    if (count > 4)
        count = 4;

    enumerate(instance, &count, devices);

    for (uint32_t d = 0; d < count; d++) {

        printf("\n========================================\n");
        printf("GPU %u\n", d);
        printf("========================================\n");

        check_semaphore(
            devices[d],
            getExternal,
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
            "OPAQUE_FD");

        check_semaphore(
            devices[d],
            getExternal,
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
            "SYNC_FD");
    }

    return 0;
}
