#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

static void check(
    VkPhysicalDevice pd,
    PFN_vkGetPhysicalDeviceFormatProperties getFormat,
    VkFormat format,
    const char *name)
{
    VkFormatProperties p;
    getFormat(pd, format, &p);

    printf("%-30s optimal=0x%08x linear=0x%08x buffer=0x%08x\n",
           name,
           p.optimalTilingFeatures,
           p.linearTilingFeatures,
           p.bufferFeatures);

    printf("  STORAGE_IMAGE: %s\n",
           (p.optimalTilingFeatures &
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ? "YES" : "NO");

    printf("  SAMPLED_IMAGE: %s\n",
           (p.optimalTilingFeatures &
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? "YES" : "NO");

    printf("  COLOR_ATTACHMENT: %s\n",
           (p.optimalTilingFeatures &
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ? "YES" : "NO");

    printf("  BLIT_SRC: %s\n",
           (p.optimalTilingFeatures &
            VK_FORMAT_FEATURE_BLIT_SRC_BIT) ? "YES" : "NO");

    printf("  BLIT_DST: %s\n",
           (p.optimalTilingFeatures &
            VK_FORMAT_FEATURE_BLIT_DST_BIT) ? "YES" : "NO");
}

int main(void)
{
    printf("=== XCLIPSE 940 IMAGE FORMAT PROBE ===\n");

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

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LSFG-Format-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-Format-Probe",
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

    PFN_vkGetPhysicalDeviceFormatProperties getFormat =
        (PFN_vkGetPhysicalDeviceFormatProperties)getProc(
            instance, "vkGetPhysicalDeviceFormatProperties");

    uint32_t count = 0;
    enumerate(instance, &count, NULL);

    VkPhysicalDevice devices[4];

    if (count > 4)
        count = 4;

    enumerate(instance, &count, devices);

    for (uint32_t d = 0; d < count; d++) {

        printf("\n=== GPU %u ===\n", d);

        check(devices[d], getFormat,
              VK_FORMAT_R8G8B8A8_UNORM,
              "R8G8B8A8_UNORM");

        check(devices[d], getFormat,
              VK_FORMAT_R8G8B8A8_SRGB,
              "R8G8B8A8_SRGB");

        check(devices[d], getFormat,
              VK_FORMAT_B8G8R8A8_UNORM,
              "B8G8R8A8_UNORM");

        check(devices[d], getFormat,
              VK_FORMAT_B8G8R8A8_SRGB,
              "B8G8R8A8_SRGB");

        check(devices[d], getFormat,
              VK_FORMAT_R16G16B16A16_SFLOAT,
              "R16G16B16A16_SFLOAT");
    }

    return 0;
}
