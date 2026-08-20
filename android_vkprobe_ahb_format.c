#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

static void check(
    PFN_vkGetPhysicalDeviceImageFormatProperties2 fn,
    VkPhysicalDevice pd,
    VkFormat format,
    const char *name)
{
    VkPhysicalDeviceExternalImageFormatInfo external = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .pNext = NULL,
        .handleType =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID
    };

    VkPhysicalDeviceImageFormatInfo2 info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &external,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage =
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .flags = 0
    };

    VkExternalImageFormatProperties externalProps = {
        .sType =
            VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES
    };

    VkImageFormatProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &externalProps
    };

    VkResult r = fn(pd, &info, &props);

    printf("\n%s\n", name);
    printf("  result: %d\n", r);

    if (r == VK_SUCCESS) {
        printf("  maxExtent: %u x %u x %u\n",
               props.imageFormatProperties.maxExtent.width,
               props.imageFormatProperties.maxExtent.height,
               props.imageFormatProperties.maxExtent.depth);

        printf("  maxMipLevels: %u\n",
               props.imageFormatProperties.maxMipLevels);

        printf("  maxArrayLayers: %u\n",
               props.imageFormatProperties.maxArrayLayers);

        printf("  sampleCounts: 0x%08x\n",
               props.imageFormatProperties.sampleCounts);

        printf("  externalFeatures: 0x%08x\n",
               externalProps.externalMemoryProperties
                   .externalMemoryFeatures);

        printf("  compatibleHandleTypes: 0x%08x\n",
               externalProps.externalMemoryProperties
                   .compatibleHandleTypes);
    }
}

int main(void)
{
    printf("=== XCLIPSE 940 AHB IMAGE FORMAT PROBE ===\n");

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
        .pApplicationName = "LSFG-AHB-Format-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-AHB-Format-Probe",
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

    PFN_vkGetPhysicalDeviceImageFormatProperties2 getFormat =
        (PFN_vkGetPhysicalDeviceImageFormatProperties2)getProc(
            instance,
            "vkGetPhysicalDeviceImageFormatProperties2");

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

        check(
            getFormat,
            devices[d],
            VK_FORMAT_R8G8B8A8_UNORM,
            "R8G8B8A8_UNORM");

        check(
            getFormat,
            devices[d],
            VK_FORMAT_R16G16B16A16_SFLOAT,
            "R16G16B16A16_SFLOAT");
    }

    return 0;
}
