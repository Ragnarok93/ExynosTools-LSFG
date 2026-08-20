#include <stdio.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

int main(void)
{
    void *lib = dlopen(
        "/system/lib64/libvulkan.so",
        RTLD_NOW | RTLD_LOCAL);

    if (!lib) {
        printf("dlopen failed: %s\n", dlerror());
        return 1;
    }

    PFN_vkGetInstanceProcAddr gpa =
        (PFN_vkGetInstanceProcAddr)dlsym(
            lib,
            "vkGetInstanceProcAddr");

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance)dlsym(
            lib,
            "vkCreateInstance");

    if (!gpa || !createInstance) {
        printf("Vulkan entry points missing\n");
        return 2;
    }

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LSFG Image Capability Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_2
    };

    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app
    };

    VkInstance instance = VK_NULL_HANDLE;

    VkResult r = createInstance(
        &ici,
        NULL,
        &instance);

    printf("vkCreateInstance: %d\n", r);

    if (r != VK_SUCCESS)
        return 3;

    PFN_vkEnumeratePhysicalDevices enumerate =
        (PFN_vkEnumeratePhysicalDevices)
        gpa(instance, "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceProperties getProps =
        (PFN_vkGetPhysicalDeviceProperties)
        gpa(instance, "vkGetPhysicalDeviceProperties");

    PFN_vkGetPhysicalDeviceFormatProperties getFormats =
        (PFN_vkGetPhysicalDeviceFormatProperties)
        gpa(instance, "vkGetPhysicalDeviceFormatProperties");

    if (!enumerate || !getProps || !getFormats) {
        printf("Required functions missing\n");
        return 4;
    }

    uint32_t count = 0;

    r = enumerate(
        instance,
        &count,
        NULL);

    if (r != VK_SUCCESS || count == 0) {
        printf("No physical device\n");
        return 5;
    }

    VkPhysicalDevice devices[8];

    if (count > 8)
        count = 8;

    r = enumerate(
        instance,
        &count,
        devices);

    if (r != VK_SUCCESS)
        return 6;

    VkPhysicalDevice physical = devices[0];

    VkPhysicalDeviceProperties props;

    getProps(
        physical,
        &props);

    printf("GPU: %s\n", props.deviceName);
    printf(
        "Vulkan: %u.%u.%u\n",
        VK_VERSION_MAJOR(props.apiVersion),
        VK_VERSION_MINOR(props.apiVersion),
        VK_VERSION_PATCH(props.apiVersion));

    VkFormat formats[] = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT
    };

    const char *names[] = {
        "R8G8B8A8_UNORM",
        "B8G8R8A8_UNORM",
        "R16G16B16A16_SFLOAT"
    };

    for (unsigned i = 0; i < 3; i++) {
        VkFormatProperties fp;

        getFormats(
            physical,
            formats[i],
            &fp);

        printf("\nFORMAT %s\n", names[i]);

        printf(
            "optimalTilingFeatures = 0x%08x\n",
            fp.optimalTilingFeatures);

        printf(
            "linearTilingFeatures  = 0x%08x\n",
            fp.linearTilingFeatures);

        printf(
            "bufferFeatures        = 0x%08x\n",
            fp.bufferFeatures);

        printf(
            "STORAGE_IMAGE: %s\n",
            (fp.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
                ? "YES"
                : "NO");

        printf(
            "SAMPLED_IMAGE: %s\n",
            (fp.optimalTilingFeatures &
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
                ? "YES"
                : "NO");

        printf(
            "TRANSFER_SRC: %s\n",
            (fp.optimalTilingFeatures &
             VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)
                ? "YES"
                : "NO");

        printf(
            "TRANSFER_DST: %s\n",
            (fp.optimalTilingFeatures &
             VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
                ? "YES"
                : "NO");
    }

    return 0;
}
