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
        VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME,
        "VK_EXT_robustness2",
        "VK_EXT_transform_feedback",
        "VK_KHR_maintenance5",
        "VK_KHR_maintenance6",
        "VK_EXT_extended_dynamic_state3",
        "VK_EXT_graphics_pipeline_library",
        "VK_EXT_descriptor_buffer"
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
    void **tail = &f13.pNext;

#ifdef VK_EXT_robustness2
    VkPhysicalDeviceRobustness2FeaturesEXT robustness2;
    memset(&robustness2, 0, sizeof(robustness2));
    robustness2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
    *tail = &robustness2;
    tail = &robustness2.pNext;
#endif

#ifdef VK_EXT_transform_feedback
    VkPhysicalDeviceTransformFeedbackFeaturesEXT transform_feedback;
    memset(&transform_feedback, 0, sizeof(transform_feedback));
    transform_feedback.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
    *tail = &transform_feedback;
    tail = &transform_feedback.pNext;
#endif

#ifdef VK_KHR_maintenance5
    VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5;
    memset(&maintenance5, 0, sizeof(maintenance5));
    maintenance5.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR;
    *tail = &maintenance5;
    tail = &maintenance5.pNext;
#endif

#ifdef VK_KHR_maintenance6
    VkPhysicalDeviceMaintenance6FeaturesKHR maintenance6;
    memset(&maintenance6, 0, sizeof(maintenance6));
    maintenance6.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR;
    *tail = &maintenance6;
    tail = &maintenance6.pNext;
#endif

#ifdef VK_EXT_extended_dynamic_state3
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamic_state3;
    memset(&dynamic_state3, 0, sizeof(dynamic_state3));
    dynamic_state3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
    *tail = &dynamic_state3;
    tail = &dynamic_state3.pNext;
#endif

#ifdef VK_EXT_graphics_pipeline_library
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT graphics_pipeline_library;
    memset(&graphics_pipeline_library, 0, sizeof(graphics_pipeline_library));
    graphics_pipeline_library.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT;
    *tail = &graphics_pipeline_library;
    tail = &graphics_pipeline_library.pNext;
#endif

#ifdef VK_EXT_descriptor_buffer
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer;
    memset(&descriptor_buffer, 0, sizeof(descriptor_buffer));
    descriptor_buffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    *tail = &descriptor_buffer;
    tail = &descriptor_buffer.pNext;
#endif

    *tail = NULL;
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
    printf("textureCompressionBC:         %s\n",
           f2.features.textureCompressionBC ? "YES" : "NO");
    printf("robustBufferAccess:           %s\n",
           f2.features.robustBufferAccess ? "YES" : "NO");

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
    printf("descriptorBindingSampledImageUpdateAfterBind: %s\n",
           f12.descriptorBindingSampledImageUpdateAfterBind ? "YES" : "NO");
    printf("descriptorBindingUpdateUnusedWhilePending: %s\n",
           f12.descriptorBindingUpdateUnusedWhilePending ? "YES" : "NO");
    printf("vulkanMemoryModel:             %s\n",
           f12.vulkanMemoryModel ? "YES" : "NO");
    printf("vulkanMemoryModelDeviceScope:  %s\n",
           f12.vulkanMemoryModelDeviceScope ? "YES" : "NO");
    printf("shaderFloat16:                  %s\n",
           f12.shaderFloat16 ? "YES" : "NO");
    printf("shaderInt8:                    %s\n",
           f12.shaderInt8 ? "YES" : "NO");

    printf("\n=== Vulkan 1.3 features ===\n");
    printf("robustImageAccess:              %s\n",
           f13.robustImageAccess ? "YES" : "NO");
    printf("synchronization2:               %s\n",
           f13.synchronization2 ? "YES" : "NO");
    printf("dynamicRendering:               %s\n",
           f13.dynamicRendering ? "YES" : "NO");
    printf("maintenance4:                  %s\n",
           f13.maintenance4 ? "YES" : "NO");
    printf("subgroupSizeControl:            %s\n",
           f13.subgroupSizeControl ? "YES" : "NO");

    printf("\n=== DXVK 2.x extension features (advertised, not semantic proof) ===\n");
#ifdef VK_EXT_robustness2
    printf("robustBufferAccess2:             %s\n",
           robustness2.robustBufferAccess2 ? "YES" : "NO");
    printf("robustImageAccess2:              %s\n",
           robustness2.robustImageAccess2 ? "YES" : "NO");
    printf("nullDescriptor:                  %s\n",
           robustness2.nullDescriptor ? "YES" : "NO");
#else
    printf("robustness2 feature structs:     HEADER_UNAVAILABLE\n");
#endif

#ifdef VK_EXT_transform_feedback
    printf("transformFeedback:                %s\n",
           transform_feedback.transformFeedback ? "YES" : "NO");
    printf("geometryStreams:                  %s\n",
           transform_feedback.geometryStreams ? "YES" : "NO");
#else
    printf("transform feedback structs:       HEADER_UNAVAILABLE\n");
#endif

#ifdef VK_KHR_maintenance5
    printf("maintenance5:                     %s\n",
           maintenance5.maintenance5 ? "YES" : "NO");
#else
    printf("maintenance5 structs:             HEADER_UNAVAILABLE\n");
#endif

#ifdef VK_KHR_maintenance6
    printf("maintenance6:                     %s\n",
           maintenance6.maintenance6 ? "YES" : "NO");
#else
    printf("maintenance6 structs:             HEADER_UNAVAILABLE\n");
#endif

#ifdef VK_EXT_extended_dynamic_state3
    printf("eds3RasterizationSamples:         %s\n",
           dynamic_state3.extendedDynamicState3RasterizationSamples ? "YES" : "NO");
    printf("eds3SampleMask:                   %s\n",
           dynamic_state3.extendedDynamicState3SampleMask ? "YES" : "NO");
    printf("eds3AlphaToCoverageEnable:        %s\n",
           dynamic_state3.extendedDynamicState3AlphaToCoverageEnable ? "YES" : "NO");
#else
    printf("extended dynamic state3 structs:  HEADER_UNAVAILABLE\n");
#endif

#ifdef VK_EXT_graphics_pipeline_library
    printf("graphicsPipelineLibrary:          %s\n",
           graphics_pipeline_library.graphicsPipelineLibrary ? "YES" : "NO");
#else
    printf("graphics pipeline library structs: HEADER_UNAVAILABLE\n");
#endif

#ifdef VK_EXT_descriptor_buffer
    printf("descriptorBuffer:                 %s\n",
           descriptor_buffer.descriptorBuffer ? "YES" : "NO");
#else
    printf("descriptor buffer structs:        HEADER_UNAVAILABLE\n");
#endif
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
