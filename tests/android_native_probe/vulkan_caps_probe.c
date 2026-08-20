#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

static void print_version(const char *name, uint32_t v) {
    printf("%s: %u.%u.%u\n",
           name,
           VK_VERSION_MAJOR(v),
           VK_VERSION_MINOR(v),
           VK_VERSION_PATCH(v));
}

static int has_extension(
    uint32_t count,
    const VkExtensionProperties *exts,
    const char *name)
{
    for (uint32_t i = 0; i < count; ++i)
        if (strcmp(exts[i].extensionName, name) == 0)
            return 1;

    return 0;
}

static void check_device_extensions(VkPhysicalDevice device) {
    uint32_t count = 0;
    VkResult r = vkEnumerateDeviceExtensionProperties(
        device, NULL, &count, NULL);

    printf("\n=== Device extensions ===\n");
    printf("enumerate result: %d\n", r);
    printf("count: %u\n", count);

    if (r != VK_SUCCESS || count == 0)
        return;

    VkExtensionProperties exts[count];

    r = vkEnumerateDeviceExtensionProperties(
        device, NULL, &count, exts);

    printf("enumerate result: %d\n", r);

    const char *wanted[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
        VK_KHR_SHADER_SUBGROUP_EXTENDED_TYPES_EXTENSION_NAME,
        VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
        VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
        "VK_ANDROID_external_memory_android_hardware_buffer",
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
        VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME
    };

    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); ++i)
        printf("%-55s %s\n",
               wanted[i],
               has_extension(count, exts, wanted[i]) ? "YES" : "NO");

    printf("\n--- All extensions ---\n");
    for (uint32_t i = 0; i < count; ++i)
        printf("%s\n", exts[i].extensionName);
}

static void print_features(VkPhysicalDevice device) {
    VkPhysicalDeviceFeatures2 f2;
    memset(&f2, 0, sizeof(f2));
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceVulkan12Features f12;
    memset(&f12, 0, sizeof(f12));
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    VkPhysicalDeviceVulkan13Features f13;
    memset(&f13, 0, sizeof(f13));
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    f2.pNext = &f12;
    f12.pNext = &f13;

    vkGetPhysicalDeviceFeatures2(device, &f2);

    printf("\n=== Core features ===\n");
    printf("geometryShader:              %s\n",
           f2.features.geometryShader ? "YES" : "NO");
    printf("shaderInt64:                  %s\n",
           f2.features.shaderInt64 ? "YES" : "NO");
    printf("samplerAnisotropy:            %s\n",
           f2.features.samplerAnisotropy ? "YES" : "NO");
    printf("multiDrawIndirect:            %s\n",
           f2.features.multiDrawIndirect ? "YES" : "NO");
    printf("textureCompressionASTC_LDR:   %s\n",
           f2.features.textureCompressionASTC_LDR ? "YES" : "NO");

    printf("\n=== Vulkan 1.2 features ===\n");
    printf("timelineSemaphore:            %s\n",
           f12.timelineSemaphore ? "YES" : "NO");
    printf("bufferDeviceAddress:           %s\n",
           f12.bufferDeviceAddress ? "YES" : "NO");
    printf("descriptorIndexing:             %s\n",
           f12.descriptorIndexing ? "YES" : "NO");
    printf("runtimeDescriptorArray:        %s\n",
           f12.runtimeDescriptorArray ? "YES" : "NO");
    printf("descriptorBindingPartiallyBound:%s\n",
           f12.descriptorBindingPartiallyBound ? "YES" : "NO");
    printf("descriptorBindingVariableDescriptorCount: %s\n",
           f12.descriptorBindingVariableDescriptorCount ? "YES" : "NO");
    printf("shaderFloat16:                  %s\n",
           f12.shaderFloat16 ? "YES" : "NO");
    printf("shaderInt8:                    %s\n",
           f12.shaderInt8 ? "YES" : "NO");

    printf("\n=== Vulkan 1.3 features ===\n");
    printf("synchronization2:               %s\n",
           f13.synchronization2 ? "YES" : "NO");
    printf("dynamicRendering:               %s\n",
           f13.dynamicRendering ? "YES" : "NO");
    printf("maintenance4:                  %s\n",
           f13.maintenance4 ? "YES" : "NO");
}

static void print_queue_families(VkPhysicalDevice device) {
    uint32_t count = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);

    printf("\n=== Queue families ===\n");
    printf("count: %u\n", count);

    VkQueueFamilyProperties queues[count];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, queues);

    for (uint32_t i = 0; i < count; ++i) {
        printf("queue %u:\n", i);
        printf("  flags: 0x%x\n", queues[i].queueFlags);
        printf("  count: %u\n", queues[i].queueCount);

        if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            printf("  graphics: YES\n");
        if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            printf("  compute: YES\n");
        if (queues[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
            printf("  transfer: YES\n");
    }
}

int main(void) {
    uint32_t loader_version = VK_API_VERSION_1_0;

    VkResult r = vkEnumerateInstanceVersion(&loader_version);

    if (r != VK_SUCCESS) {
        printf("vkEnumerateInstanceVersion failed: %d\n", r);
        return 1;
    }

    print_version("Vulkan loader API", loader_version);

    VkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vulkan_caps_probe";
    app.applicationVersion = 1;
    app.pEngineName = "probe";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;

    r = vkCreateInstance(&ci, NULL, &instance);

    printf("vkCreateInstance: %d\n", r);

    if (r != VK_SUCCESS)
        return 2;

    uint32_t count = 0;

    r = vkEnumeratePhysicalDevices(instance, &count, NULL);

    printf("physical device count: %u\n", count);

    if (r != VK_SUCCESS || count == 0) {
        vkDestroyInstance(instance, NULL);
        return 3;
    }

    VkPhysicalDevice devices[count];

    r = vkEnumeratePhysicalDevices(instance, &count, devices);

    if (r != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 4;
    }

    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties props;
        memset(&props, 0, sizeof(props));

        vkGetPhysicalDeviceProperties(devices[i], &props);

        printf("\n========================================\n");
        printf("GPU %u\n", i);
        printf("========================================\n");

        printf("name:   %s\n", props.deviceName);
        printf("vendor: 0x%04x\n", props.vendorID);
        printf("device: 0x%x\n", props.deviceID);
        printf("type:   %d\n", props.deviceType);

        print_version("device API", props.apiVersion);
        print_version("driver", props.driverVersion);

        print_queue_families(devices[i]);
        check_device_extensions(devices[i]);
        print_features(devices[i]);
    }

    vkDestroyInstance(instance, NULL);
    return 0;
}
