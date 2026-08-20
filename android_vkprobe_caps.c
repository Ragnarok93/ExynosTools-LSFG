#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

static void print_bool(const char *name, VkBool32 v)
{
    printf("  %-45s %s\n", name, v ? "YES" : "NO");
}

static void print_ext(const VkExtensionProperties *e)
{
    printf("  %-60s %u\n", e->extensionName, e->specVersion);
}

int main(void)
{
    printf("=== XCLIPSE 940 VULKAN CAPABILITY PROBE ===\n");
    fflush(stdout);

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

    if (!getProc || !createInstance) {
        printf("Required loader symbols missing\n");
        return 2;
    }

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "LSFG-Capability-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-Capability-Probe",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_0
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

    VkResult r = createInstance(&ci, NULL, &instance);

    printf("vkCreateInstance: %d\n", r);

    if (r != VK_SUCCESS)
        return 3;

    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)getProc(
            instance, "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceProperties getProperties =
        (PFN_vkGetPhysicalDeviceProperties)getProc(
            instance, "vkGetPhysicalDeviceProperties");

    PFN_vkGetPhysicalDeviceFeatures getFeatures =
        (PFN_vkGetPhysicalDeviceFeatures)getProc(
            instance, "vkGetPhysicalDeviceFeatures");

    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilies =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)getProc(
            instance, "vkGetPhysicalDeviceQueueFamilyProperties");

    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensions =
        (PFN_vkEnumerateDeviceExtensionProperties)getProc(
            instance, "vkEnumerateDeviceExtensionProperties");

    if (!enumeratePhysicalDevices ||
        !getProperties ||
        !getFeatures ||
        !getQueueFamilies ||
        !enumerateDeviceExtensions) {
        printf("Required physical-device functions unavailable\n");
        return 4;
    }

    uint32_t count = 0;

    r = enumeratePhysicalDevices(instance, &count, NULL);

    printf("\n=== PHYSICAL DEVICES ===\n");
    printf("Result: %d\n", r);
    printf("Count: %u\n", count);

    if (r != VK_SUCCESS || count == 0)
        return 5;

    VkPhysicalDevice devices[8];

    if (count > 8)
        count = 8;

    r = enumeratePhysicalDevices(instance, &count, devices);

    if (r != VK_SUCCESS)
        return 6;

    for (uint32_t d = 0; d < count; d++) {

        VkPhysicalDevice gpu = devices[d];

        VkPhysicalDeviceProperties props;
        VkPhysicalDeviceFeatures features;

        memset(&props, 0, sizeof(props));
        memset(&features, 0, sizeof(features));

        getProperties(gpu, &props);
        getFeatures(gpu, &features);

        printf("\n========================================\n");
        printf("GPU %u\n", d);
        printf("========================================\n");

        printf("Device name      : %s\n", props.deviceName);
        printf("Vendor ID        : 0x%04x\n", props.vendorID);
        printf("Device ID        : 0x%08x\n", props.deviceID);
        printf("Device type      : %u\n", props.deviceType);
        printf("Driver version   : 0x%08x\n", props.driverVersion);

        printf("Vulkan API       : %u.%u.%u\n",
               VK_VERSION_MAJOR(props.apiVersion),
               VK_VERSION_MINOR(props.apiVersion),
               VK_VERSION_PATCH(props.apiVersion));

        printf("\n=== DEVICE EXTENSIONS ===\n");

        uint32_t extCount = 0;

        r = enumerateDeviceExtensions(
            gpu, NULL, &extCount, NULL);

        printf("Extension query: %d\n", r);
        printf("Extension count: %u\n", extCount);

        if (r == VK_SUCCESS && extCount) {

            VkExtensionProperties *ext =
                calloc(extCount, sizeof(*ext));

            if (!ext)
                return 7;

            r = enumerateDeviceExtensions(
                gpu, NULL, &extCount, ext);

            if (r == VK_SUCCESS) {
                for (uint32_t i = 0; i < extCount; i++)
                    print_ext(&ext[i]);
            }

            free(ext);
        }

        printf("\n=== QUEUE FAMILIES ===\n");

        uint32_t queueCount = 0;

        getQueueFamilies(
            gpu,
            &queueCount,
            NULL);

        printf("Queue family count: %u\n", queueCount);

        if (queueCount) {

            VkQueueFamilyProperties *queues =
                calloc(queueCount, sizeof(*queues));

            if (!queues)
                return 8;

            getQueueFamilies(
                gpu,
                &queueCount,
                queues);

            for (uint32_t i = 0; i < queueCount; i++) {

                printf("\nQueue family %u\n", i);

                printf("  queueCount    : %u\n",
                       queues[i].queueCount);

                printf("  queueFlags    : 0x%08x\n",
                       queues[i].queueFlags);

                printf("    GRAPHICS     : %s\n",
                       (queues[i].queueFlags &
                        VK_QUEUE_GRAPHICS_BIT) ? "YES" : "NO");

                printf("    COMPUTE      : %s\n",
                       (queues[i].queueFlags &
                        VK_QUEUE_COMPUTE_BIT) ? "YES" : "NO");

                printf("    TRANSFER     : %s\n",
                       (queues[i].queueFlags &
                        VK_QUEUE_TRANSFER_BIT) ? "YES" : "NO");

                printf("    SPARSE       : %s\n",
                       (queues[i].queueFlags &
                        VK_QUEUE_SPARSE_BINDING_BIT) ? "YES" : "NO");

#ifdef VK_QUEUE_VIDEO_DECODE_BIT_KHR
                printf("    VIDEO DECODE : %s\n",
                       (queues[i].queueFlags &
                        VK_QUEUE_VIDEO_DECODE_BIT_KHR) ? "YES" : "NO");
#endif

#ifdef VK_QUEUE_VIDEO_ENCODE_BIT_KHR
                printf("    VIDEO ENCODE : %s\n",
                       (queues[i].queueFlags &
                        VK_QUEUE_VIDEO_ENCODE_BIT_KHR) ? "YES" : "NO");
#endif
            }

            free(queues);
        }

        printf("\n=== CORE FEATURES ===\n");

        print_bool("robustBufferAccess",
                   features.robustBufferAccess);

        print_bool("fullDrawIndexUint32",
                   features.fullDrawIndexUint32);

        print_bool("imageCubeArray",
                   features.imageCubeArray);

        print_bool("independentBlend",
                   features.independentBlend);

        print_bool("geometryShader",
                   features.geometryShader);

        print_bool("tessellationShader",
                   features.tessellationShader);

        print_bool("sampleRateShading",
                   features.sampleRateShading);

        print_bool("dualSrcBlend",
                   features.dualSrcBlend);

        print_bool("logicOp",
                   features.logicOp);

        print_bool("multiDrawIndirect",
                   features.multiDrawIndirect);

        print_bool("drawIndirectFirstInstance",
                   features.drawIndirectFirstInstance);

        print_bool("depthClamp",
                   features.depthClamp);

        print_bool("depthBiasClamp",
                   features.depthBiasClamp);

        print_bool("fillModeNonSolid",
                   features.fillModeNonSolid);

        print_bool("depthBounds",
                   features.depthBounds);

        print_bool("wideLines",
                   features.wideLines);

        print_bool("largePoints",
                   features.largePoints);

        print_bool("alphaToOne",
                   features.alphaToOne);

        print_bool("multiViewport",
                   features.multiViewport);

        print_bool("samplerAnisotropy",
                   features.samplerAnisotropy);

        print_bool("textureCompressionBC",
                   features.textureCompressionBC);

        print_bool("occlusionQueryPrecise",
                   features.occlusionQueryPrecise);

        print_bool("pipelineStatisticsQuery",
                   features.pipelineStatisticsQuery);

        print_bool("vertexPipelineStoresAndAtomics",
                   features.vertexPipelineStoresAndAtomics);

        print_bool("fragmentStoresAndAtomics",
                   features.fragmentStoresAndAtomics);

        print_bool("shaderTessellationAndGeometryPointSize",
                   features.shaderTessellationAndGeometryPointSize);

        print_bool("shaderImageGatherExtended",
                   features.shaderImageGatherExtended);

        print_bool("shaderStorageImageExtendedFormats",
                   features.shaderStorageImageExtendedFormats);

        print_bool("shaderStorageImageMultisample",
                   features.shaderStorageImageMultisample);

        print_bool("shaderStorageImageReadWithoutFormat",
                   features.shaderStorageImageReadWithoutFormat);

        print_bool("shaderStorageImageWriteWithoutFormat",
                   features.shaderStorageImageWriteWithoutFormat);

        print_bool("shaderUniformBufferArrayDynamicIndexing",
                   features.shaderUniformBufferArrayDynamicIndexing);

        print_bool("shaderStorageBufferArrayDynamicIndexing",
                   features.shaderStorageBufferArrayDynamicIndexing);

        print_bool("shaderStorageImageArrayDynamicIndexing",
                   features.shaderStorageImageArrayDynamicIndexing);

        print_bool("shaderClipDistance",
                   features.shaderClipDistance);

        print_bool("shaderCullDistance",
                   features.shaderCullDistance);

        print_bool("shaderFloat64",
                   features.shaderFloat64);

        print_bool("shaderInt64",
                   features.shaderInt64);

        print_bool("shaderInt16",
                   features.shaderInt16);

        printf("\n=== IMPORTANT LSFG EXTENSION CHECKS ===\n");

        const char *wanted[] = {
            "VK_KHR_swapchain",
            "VK_KHR_external_memory",
            "VK_KHR_external_memory_fd",
            "VK_KHR_external_semaphore",
            "VK_KHR_external_semaphore_fd",
            "VK_KHR_timeline_semaphore",
            "VK_KHR_synchronization2",
            "VK_KHR_dynamic_rendering",
            "VK_EXT_descriptor_indexing",
            "VK_KHR_storage_buffer_storage_class",
            "VK_ANDROID_external_memory_android_hardware_buffer",
            "VK_KHR_maintenance1",
            "VK_KHR_maintenance2",
            "VK_KHR_maintenance3",
            "VK_KHR_buffer_device_address",
            "VK_KHR_shader_subgroup_extended_types"
        };

        uint32_t wantedCount =
            sizeof(wanted) / sizeof(wanted[0]);

        uint32_t found[sizeof(wanted) / sizeof(wanted[0])];

        memset(found, 0, sizeof(found));

        /*
         * Re-enumerate extensions and compare names.
         */
        uint32_t n = 0;

        enumerateDeviceExtensions(
            gpu, NULL, &n, NULL);

        VkExtensionProperties *all =
            calloc(n, sizeof(*all));

        if (all) {

            enumerateDeviceExtensions(
                gpu, NULL, &n, all);

            for (uint32_t w = 0; w < wantedCount; w++) {

                for (uint32_t e = 0; e < n; e++) {

                    if (strcmp(
                            wanted[w],
                            all[e].extensionName) == 0) {

                        found[w] = 1;
                        break;
                    }
                }

                printf("  %-55s %s\n",
                       wanted[w],
                       found[w] ? "PRESENT" : "ABSENT");
            }

            free(all);
        }

        printf("\n=== FEATURE STRUCTURES ===\n");

        VkPhysicalDeviceFeatures2 features2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = NULL
        };

        VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
            .pNext = NULL
        };

        VkPhysicalDeviceSynchronization2Features sync2 = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            .pNext = NULL
        };

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
            .pNext = NULL
        };

        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
            .pNext = NULL
        };

        features2.pNext = &timeline;
        timeline.pNext = &sync2;
        sync2.pNext = &dynamicRendering;
        dynamicRendering.pNext = &descriptorIndexing;

        PFN_vkGetPhysicalDeviceFeatures2 getFeatures2 =
            (PFN_vkGetPhysicalDeviceFeatures2)getProc(
                instance, "vkGetPhysicalDeviceFeatures2");

        if (getFeatures2) {

            getFeatures2(gpu, &features2);

            printf("timelineSemaphore     : %s\n",
                   timeline.timelineSemaphore ? "YES" : "NO");

            printf("synchronization2      : %s\n",
                   sync2.synchronization2 ? "YES" : "NO");

            printf("dynamicRendering      : %s\n",
                   dynamicRendering.dynamicRendering ? "YES" : "NO");

            printf("descriptorIndexing    : %s\n",
                   (descriptorIndexing.runtimeDescriptorArray || descriptorIndexing.descriptorBindingPartiallyBound || descriptorIndexing.descriptorBindingVariableDescriptorCount) ? "PARTIAL/YES" : "NO");

            printf("runtimeDescriptorArray: %s\n",
                   descriptorIndexing.runtimeDescriptorArray ? "YES" : "NO");

            printf("partiallyBound        : %s\n",
                   descriptorIndexing.descriptorBindingPartiallyBound ? "YES" : "NO");

            printf("variableDescriptorCount: %s\n",
                   descriptorIndexing.descriptorBindingVariableDescriptorCount ? "YES" : "NO");
        } else {
            printf("vkGetPhysicalDeviceFeatures2 unavailable\n");
        }
    }

    printf("\n=== PROBE COMPLETE ===\n");

    return 0;
}
