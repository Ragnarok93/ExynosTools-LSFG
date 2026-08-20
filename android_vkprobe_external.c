#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

static void print_handle(
    VkPhysicalDevice pd,
    PFN_vkGetPhysicalDeviceExternalBufferProperties getExternal,
    VkExternalMemoryHandleTypeFlagBits handle,
    const char *name)
{
    VkPhysicalDeviceExternalBufferInfo info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
        .pNext = NULL,
        .flags = 0,
        .usage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .handleType = handle
    };

    VkExternalBufferProperties props = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES,
        .pNext = NULL
    };

    getExternal(pd, &info, &props);

    printf("\n%s\n", name);
    printf("  externalMemoryFeatures    : 0x%08x\n",
           props.externalMemoryProperties.externalMemoryFeatures);
    printf("  compatibleHandleTypes     : 0x%08x\n",
           props.externalMemoryProperties.compatibleHandleTypes);
    printf("  exportFromImportedHandles : 0x%08x\n",
           props.externalMemoryProperties.exportFromImportedHandleTypes);

    printf("  IMPORTABLE: %s\n",
           (props.externalMemoryProperties.externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ? "YES" : "NO");

    printf("  EXPORTABLE: %s\n",
           (props.externalMemoryProperties.externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) ? "YES" : "NO");

    printf("  DEDICATED_ONLY: %s\n",
           (props.externalMemoryProperties.externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) ? "YES" : "NO");
}

int main(void)
{
    printf("=== XCLIPSE 940 EXTERNAL MEMORY PROBE ===\n");

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

    if (!getProc || !createInstance) {
        printf("Required loader symbols missing\n");
        return 2;
    }

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LSFG-External-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-External-Probe",
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
        return 3;

    PFN_vkEnumeratePhysicalDevices enumerate =
        (PFN_vkEnumeratePhysicalDevices)getProc(
            instance, "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceExternalBufferProperties getExternal =
        (PFN_vkGetPhysicalDeviceExternalBufferProperties)getProc(
            instance, "vkGetPhysicalDeviceExternalBufferProperties");

    if (!enumerate || !getExternal) {
        printf("Required external-memory functions missing\n");
        return 4;
    }

    uint32_t count = 0;

    r = enumerate(instance, &count, NULL);

    if (r != VK_SUCCESS || count == 0) {
        printf("Physical-device enumeration failed: %d\n", r);
        return 5;
    }

    VkPhysicalDevice devices[4];

    if (count > 4)
        count = 4;

    r = enumerate(instance, &count, devices);

    if (r != VK_SUCCESS)
        return 6;

    for (uint32_t d = 0; d < count; d++) {

        printf("\n========================================\n");
        printf("GPU %u\n", d);
        printf("========================================\n");

        print_handle(
            devices[d],
            getExternal,
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
            "OPAQUE_FD");

        print_handle(
            devices[d],
            getExternal,
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            "DMA_BUF");

        print_handle(
            devices[d],
            getExternal,
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
            "ANDROID_HARDWARE_BUFFER");
    }

    return 0;
}
