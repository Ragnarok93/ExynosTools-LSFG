#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

int main(void) {
    uint32_t api_version = VK_API_VERSION_1_0;

    VkResult r = vkEnumerateInstanceVersion(&api_version);
    if (r != VK_SUCCESS) {
        printf("vkEnumerateInstanceVersion failed: %d\n", r);
        return 1;
    }

    printf("Vulkan loader API: %u.%u.%u\n",
           VK_VERSION_MAJOR(api_version),
           VK_VERSION_MINOR(api_version),
           VK_VERSION_PATCH(api_version));

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "android_native_probe",
        .applicationVersion = 1,
        .pEngineName = "probe",
        .engineVersion = 1,
        .apiVersion = api_version
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

    r = vkCreateInstance(&ci, NULL, &instance);
    printf("vkCreateInstance: %d\n", r);

    if (r != VK_SUCCESS)
        return 2;

    uint32_t count = 0;
    r = vkEnumeratePhysicalDevices(instance, &count, NULL);

    printf("vkEnumeratePhysicalDevices(count): %d, count=%u\n", r, count);

    if (r == VK_SUCCESS && count > 0) {
        VkPhysicalDevice *devices =
            (VkPhysicalDevice *)__builtin_alloca(
                count * sizeof(VkPhysicalDevice));

        r = vkEnumeratePhysicalDevices(instance, &count, devices);
        printf("vkEnumeratePhysicalDevices: %d\n", r);

        for (uint32_t i = 0; i < count; ++i) {
            VkPhysicalDeviceProperties props;
            memset(&props, 0, sizeof(props));

            vkGetPhysicalDeviceProperties(devices[i], &props);

            printf("GPU %u:\n", i);
            printf("  name: %s\n", props.deviceName);
            printf("  vendor: 0x%04x\n", props.vendorID);
            printf("  device: 0x%04x\n", props.deviceID);
            printf("  api: %u.%u.%u\n",
                   VK_VERSION_MAJOR(props.apiVersion),
                   VK_VERSION_MINOR(props.apiVersion),
                   VK_VERSION_PATCH(props.apiVersion));
            printf("  driver: %u.%u.%u\n",
                   VK_VERSION_MAJOR(props.driverVersion),
                   VK_VERSION_MINOR(props.driverVersion),
                   VK_VERSION_PATCH(props.driverVersion));
            printf("  type: %d\n", props.deviceType);
        }
    }

    vkDestroyInstance(instance, NULL);
    return 0;
}
