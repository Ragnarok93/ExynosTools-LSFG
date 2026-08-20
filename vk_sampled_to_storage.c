#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <vulkan/vulkan.h>

#define LOAD_DEVICE(name)                                      \
    PFN_##name name =                                         \
        (PFN_##name)getDeviceProcAddr(device, #name);         \
    if (!name) {                                               \
        printf("MISSING DEVICE FUNCTION: %s\n", #name);       \
        return 100;                                            \
    }

static uint32_t find_memory_type(
    const VkPhysicalDeviceMemoryProperties *mem,
    uint32_t typeBits,
    VkMemoryPropertyFlags wanted)
{
    for (uint32_t i = 0; i < mem->memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            (mem->memoryTypes[i].propertyFlags & wanted) == wanted)
            return i;
    }

    return UINT32_MAX;
}

static int load_spirv(
    const char *path,
    uint32_t **data,
    size_t *size)
{
    FILE *f = fopen(path, "rb");

    if (!f)
        return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -2;
    }

    long n = ftell(f);

    if (n <= 0) {
        fclose(f);
        return -3;
    }

    rewind(f);

    *data = malloc((size_t)n);

    if (!*data) {
        fclose(f);
        return -4;
    }

    if (fread(*data, 1, (size_t)n, f) != (size_t)n) {
        free(*data);
        fclose(f);
        return -5;
    }

    fclose(f);

    *size = (size_t)n;

    return 0;
}

static uint8_t quantize_unorm(unsigned value)
{
    /*
     * Input pattern uses:
     *
     * R = x / 63
     * G = y / 63
     * B = 0.25
     * A = 1.0
     *
     * Vulkan UNORM conversion rounds to nearest.
     */
    return (uint8_t)((value * 255u + 31u) / 63u);
}

static void fill_input_pattern(uint8_t *pixels)
{
    for (unsigned y = 0; y < 64; y++) {
        for (unsigned x = 0; x < 64; x++) {
            size_t offset =
                ((size_t)y * 64u + x) * 4u;

            pixels[offset + 0] = quantize_unorm(x);
            pixels[offset + 1] = quantize_unorm(y);
            pixels[offset + 2] = 64;
            pixels[offset + 3] = 255;
        }
    }
}

int main(void)
{
    printf(
        "=== XCLIPSE 940 SAMPLED -> STORAGE IMAGE PROBE ===\n");
    fflush(stdout);

    void *lib = dlopen(
        "/system/lib64/libvulkan.so",
        RTLD_NOW | RTLD_LOCAL);

    if (!lib) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }

    PFN_vkGetInstanceProcAddr getProc =
        (PFN_vkGetInstanceProcAddr)dlsym(
            lib,
            "vkGetInstanceProcAddr");

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance)dlsym(
            lib,
            "vkCreateInstance");

    if (!getProc || !createInstance) {
        printf("Loader functions missing\n");
        return 2;
    }

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LSFG Sampled Storage Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_2
    };

    VkInstanceCreateInfo instanceCI = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app
    };

    VkInstance instance = VK_NULL_HANDLE;

    VkResult r = createInstance(
        &instanceCI,
        NULL,
        &instance);

    printf("vkCreateInstance: %d\n", r);

    if (r != VK_SUCCESS)
        return 3;

    PFN_vkEnumeratePhysicalDevices enumerate =
        (PFN_vkEnumeratePhysicalDevices)
        getProc(instance, "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceProperties getProps =
        (PFN_vkGetPhysicalDeviceProperties)
        getProc(instance, "vkGetPhysicalDeviceProperties");

    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueues =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)
        getProc(instance, "vkGetPhysicalDeviceQueueFamilyProperties");

    PFN_vkGetPhysicalDeviceMemoryProperties getMemory =
        (PFN_vkGetPhysicalDeviceMemoryProperties)
        getProc(instance, "vkGetPhysicalDeviceMemoryProperties");

    PFN_vkGetPhysicalDeviceFormatProperties getFormats =
        (PFN_vkGetPhysicalDeviceFormatProperties)
        getProc(instance, "vkGetPhysicalDeviceFormatProperties");

    PFN_vkCreateDevice createDevice =
        (PFN_vkCreateDevice)
        getProc(instance, "vkCreateDevice");

    PFN_vkGetDeviceProcAddr getDeviceProcAddr =
        (PFN_vkGetDeviceProcAddr)
        getProc(instance, "vkGetDeviceProcAddr");



    if (!enumerate ||
        !getProps ||
        !getQueues ||
        !getMemory ||
        !getFormats ||
        !createDevice ||
        !getDeviceProcAddr) {

        printf("Required instance functions missing\n");
        return 4;
    }

    uint32_t physicalCount = 0;

    r = enumerate(
        instance,
        &physicalCount,
        NULL);

    if (r != VK_SUCCESS || physicalCount == 0) {
        printf("No physical devices: %d\n", r);
        return 5;
    }

    VkPhysicalDevice physicalDevices[8];

    if (physicalCount > 8)
        physicalCount = 8;

    r = enumerate(
        instance,
        &physicalCount,
        physicalDevices);

    if (r != VK_SUCCESS)
        return 6;

    VkPhysicalDevice physical = physicalDevices[0];

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

    /*
     * Verify the format supports both sampled and storage use.
     */
    VkFormatProperties formatProps;

    getFormats(
        physical,
        VK_FORMAT_R8G8B8A8_UNORM,
        &formatProps);

    printf(
        "R8G8B8A8_UNORM sampled: %s\n",
        (formatProps.optimalTilingFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
            ? "YES"
            : "NO");

    printf(
        "R8G8B8A8_UNORM storage: %s\n",
        (formatProps.optimalTilingFeatures &
         VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
            ? "YES"
            : "NO");

    if (!(formatProps.optimalTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
        !(formatProps.optimalTilingFeatures &
          VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {

        printf("Required image capabilities unavailable\n");
        return 7;
    }

    /*
     * Select dedicated compute queue when available.
     */
    uint32_t queueFamilyCount = 0;

    getQueues(
        physical,
        &queueFamilyCount,
        NULL);

    VkQueueFamilyProperties queueFamilies[8];

    if (queueFamilyCount > 8)
        queueFamilyCount = 8;

    getQueues(
        physical,
        &queueFamilyCount,
        queueFamilies);

    uint32_t computeFamily = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkQueueFlags flags =
            queueFamilies[i].queueFlags;

        if ((flags & VK_QUEUE_COMPUTE_BIT) &&
            !(flags & VK_QUEUE_GRAPHICS_BIT)) {

            computeFamily = i;
            break;
        }
    }

    if (computeFamily == UINT32_MAX) {
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            VkQueueFlags flags =
                queueFamilies[i].queueFlags;

            if ((flags & VK_QUEUE_COMPUTE_BIT) &&
                (flags & VK_QUEUE_GRAPHICS_BIT)) {

                computeFamily = i;
                break;
            }
        }
    }

    if (computeFamily == UINT32_MAX) {
        printf("No compute queue available\n");
        return 8;
    }

    printf(
        "Selected compute queue family: %u\n",
        computeFamily);

    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCI = {
        .sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = computeFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    VkDeviceCreateInfo deviceCI = {
        .sType =
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCI
    };

    VkDevice device = VK_NULL_HANDLE;

    r = createDevice(
        physical,
        &deviceCI,
        NULL,
        &device);

    printf("vkCreateDevice: %d\n", r);

    if (r != VK_SUCCESS)
        return 9;

    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage =
        (PFN_vkCmdCopyBufferToImage)getDeviceProcAddr(
            device,
            "vkCmdCopyBufferToImage");

    PFN_vkCmdCopyImageToBuffer cmdCopyImageToBuffer =
        (PFN_vkCmdCopyImageToBuffer)getDeviceProcAddr(
            device,
            "vkCmdCopyImageToBuffer");

    if (!cmdCopyBufferToImage || !cmdCopyImageToBuffer) {
        printf("Required copy commands missing\n");
        return 10;
    }

    /*
     * Device functions.
     */
    LOAD_DEVICE(vkGetDeviceQueue);
    LOAD_DEVICE(vkCreateImage);
    LOAD_DEVICE(vkGetImageMemoryRequirements);
    LOAD_DEVICE(vkAllocateMemory);
    LOAD_DEVICE(vkBindImageMemory);
    LOAD_DEVICE(vkCreateImageView);
    LOAD_DEVICE(vkCreateSampler);
    LOAD_DEVICE(vkCreateBuffer);
    LOAD_DEVICE(vkGetBufferMemoryRequirements);
    LOAD_DEVICE(vkBindBufferMemory);
    LOAD_DEVICE(vkCreateShaderModule);
    LOAD_DEVICE(vkCreateDescriptorSetLayout);
    LOAD_DEVICE(vkCreatePipelineLayout);
    LOAD_DEVICE(vkCreateComputePipelines);
    LOAD_DEVICE(vkCreateDescriptorPool);
    LOAD_DEVICE(vkAllocateDescriptorSets);
    LOAD_DEVICE(vkUpdateDescriptorSets);
    LOAD_DEVICE(vkCreateCommandPool);
    LOAD_DEVICE(vkAllocateCommandBuffers);
    LOAD_DEVICE(vkBeginCommandBuffer);
    LOAD_DEVICE(vkEndCommandBuffer);
    LOAD_DEVICE(vkCmdPipelineBarrier);
    LOAD_DEVICE(vkCmdBindPipeline);
    LOAD_DEVICE(vkCmdBindDescriptorSets);
    LOAD_DEVICE(vkCmdDispatch);
    LOAD_DEVICE(vkCreateFence);
    LOAD_DEVICE(vkQueueSubmit);
    LOAD_DEVICE(vkWaitForFences);
    LOAD_DEVICE(vkMapMemory);
    LOAD_DEVICE(vkUnmapMemory);
    LOAD_DEVICE(vkDestroyFence);
    LOAD_DEVICE(vkDestroyCommandPool);
    LOAD_DEVICE(vkDestroyDescriptorPool);
    LOAD_DEVICE(vkDestroySampler);
    LOAD_DEVICE(vkDestroyImageView);
    LOAD_DEVICE(vkDestroyImage);
    LOAD_DEVICE(vkDestroyPipeline);
    LOAD_DEVICE(vkDestroyPipelineLayout);
    LOAD_DEVICE(vkDestroyShaderModule);
    LOAD_DEVICE(vkDestroyDescriptorSetLayout);
    LOAD_DEVICE(vkDestroyBuffer);
    LOAD_DEVICE(vkFreeMemory);
    LOAD_DEVICE(vkDestroyDevice);

    VkQueue queue = VK_NULL_HANDLE;

    vkGetDeviceQueue(
        device,
        computeFamily,
        0,
        &queue);

    if (!queue) {
        printf("Failed to obtain compute queue\n");
        return 10;
    }

    /*
     * 64x64 RGBA8 input/output.
     */
    const VkExtent3D extent = {
        .width = 64,
        .height = 64,
        .depth = 1
    };

    const VkDeviceSize bufferSize =
        64u * 64u * 4u;

    /*
     * INPUT IMAGE
     *
     * Transfer destination + sampled image.
     */
    VkImageCreateInfo inputImageCI = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout =
            VK_IMAGE_LAYOUT_UNDEFINED
    };

    VkImage inputImage = VK_NULL_HANDLE;

    r = vkCreateImage(
        device,
        &inputImageCI,
        NULL,
        &inputImage);

    printf("vkCreateImage(input): %d\n", r);

    if (r != VK_SUCCESS)
        return 11;

    VkMemoryRequirements inputMemoryReq;

    vkGetImageMemoryRequirements(
        device,
        inputImage,
        &inputMemoryReq);

    VkPhysicalDeviceMemoryProperties memoryProps;

    getMemory(
        physical,
        &memoryProps);

    uint32_t deviceMemoryType =
        find_memory_type(
            &memoryProps,
            inputMemoryReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (deviceMemoryType == UINT32_MAX) {
        printf("No device-local image memory\n");
        return 12;
    }

    VkMemoryAllocateInfo inputMemoryAI = {
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize =
            inputMemoryReq.size,
        .memoryTypeIndex =
            deviceMemoryType
    };

    VkDeviceMemory inputMemory = VK_NULL_HANDLE;

    r = vkAllocateMemory(
        device,
        &inputMemoryAI,
        NULL,
        &inputMemory);

    printf("vkAllocateMemory(input): %d\n", r);

    if (r != VK_SUCCESS)
        return 13;

    r = vkBindImageMemory(
        device,
        inputImage,
        inputMemory,
        0);

    printf("vkBindImageMemory(input): %d\n", r);

    if (r != VK_SUCCESS)
        return 14;

    /*
     * OUTPUT IMAGE
     *
     * Storage image + transfer source.
     */
    VkImageCreateInfo outputImageCI = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage =
            VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout =
            VK_IMAGE_LAYOUT_UNDEFINED
    };

    VkImage outputImage = VK_NULL_HANDLE;

    r = vkCreateImage(
        device,
        &outputImageCI,
        NULL,
        &outputImage);

    printf("vkCreateImage(output): %d\n", r);

    if (r != VK_SUCCESS)
        return 15;

    VkMemoryRequirements outputMemoryReq;

    vkGetImageMemoryRequirements(
        device,
        outputImage,
        &outputMemoryReq);

    uint32_t outputMemoryType =
        find_memory_type(
            &memoryProps,
            outputMemoryReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (outputMemoryType == UINT32_MAX) {
        printf("No device-local output memory\n");
        return 16;
    }

    VkMemoryAllocateInfo outputMemoryAI = {
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize =
            outputMemoryReq.size,
        .memoryTypeIndex =
            outputMemoryType
    };

    VkDeviceMemory outputMemory = VK_NULL_HANDLE;

    r = vkAllocateMemory(
        device,
        &outputMemoryAI,
        NULL,
        &outputMemory);

    printf("vkAllocateMemory(output): %d\n", r);

    if (r != VK_SUCCESS)
        return 17;

    r = vkBindImageMemory(
        device,
        outputImage,
        outputMemory,
        0);

    printf("vkBindImageMemory(output): %d\n", r);

    if (r != VK_SUCCESS)
        return 18;

    /*
     * Host-visible staging buffer for input upload and output readback.
     */
    VkBufferCreateInfo bufferCI = {
        .sType =
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage =
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode =
            VK_SHARING_MODE_EXCLUSIVE
    };

    VkBuffer buffer = VK_NULL_HANDLE;

    r = vkCreateBuffer(
        device,
        &bufferCI,
        NULL,
        &buffer);

    printf("vkCreateBuffer(staging): %d\n", r);

    if (r != VK_SUCCESS)
        return 19;

    VkMemoryRequirements bufferMemoryReq;

    vkGetBufferMemoryRequirements(
        device,
        buffer,
        &bufferMemoryReq);

    uint32_t hostMemoryType =
        find_memory_type(
            &memoryProps,
            bufferMemoryReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (hostMemoryType == UINT32_MAX) {
        printf("No host-visible coherent staging memory\n");
        return 20;
    }

    VkMemoryAllocateInfo bufferMemoryAI = {
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize =
            bufferMemoryReq.size,
        .memoryTypeIndex =
            hostMemoryType
    };

    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;

    r = vkAllocateMemory(
        device,
        &bufferMemoryAI,
        NULL,
        &bufferMemory);

    printf("vkAllocateMemory(staging): %d\n", r);

    if (r != VK_SUCCESS)
        return 21;

    r = vkBindBufferMemory(
        device,
        buffer,
        bufferMemory,
        0);

    printf("vkBindBufferMemory(staging): %d\n", r);

    if (r != VK_SUCCESS)
        return 22;

    /*
     * Upload deterministic source pattern.
     */
    uint8_t *mapped = NULL;

    r = vkMapMemory(
        device,
        bufferMemory,
        0,
        bufferSize,
        0,
        (void **)&mapped);

    printf("vkMapMemory(upload): %d\n", r);

    if (r != VK_SUCCESS)
        return 23;

    fill_input_pattern(mapped);

    vkUnmapMemory(
        device,
        bufferMemory);

    /*
     * Load SPIR-V.
     */
    uint32_t *spirv = NULL;
    size_t spirvSize = 0;

    int loadResult = load_spirv(
        "/data/data/com.termux/files/home/ExynosTools-LSFG/lsfg_sampled_to_storage.spv",
        &spirv,
        &spirvSize);

    if (loadResult != 0) {
        printf(
            "Failed to load SPIR-V: %d\n",
            loadResult);
        return 24;
    }

    printf(
        "SPIR-V size: %zu bytes\n",
        spirvSize);

    VkShaderModuleCreateInfo shaderCI = {
        .sType =
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirvSize,
        .pCode = spirv
    };

    VkShaderModule shaderModule =
        VK_NULL_HANDLE;

    r = vkCreateShaderModule(
        device,
        &shaderCI,
        NULL,
        &shaderModule);

    free(spirv);

    printf("vkCreateShaderModule: %d\n", r);

    if (r != VK_SUCCESS)
        return 25;

    /*
     * Descriptor set:
     *
     * binding 0 = sampled image
     * binding 1 = storage image
     */
    VkDescriptorSetLayoutBinding bindings[2];

    memset(bindings, 0, sizeof(bindings));

    bindings[0].binding = 0;
    bindings[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutCI = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings
    };

    VkDescriptorSetLayout setLayout =
        VK_NULL_HANDLE;

    r = vkCreateDescriptorSetLayout(
        device,
        &setLayoutCI,
        NULL,
        &setLayout);

    printf("vkCreateDescriptorSetLayout: %d\n", r);

    if (r != VK_SUCCESS)
        return 26;

    VkPipelineLayoutCreateInfo pipelineLayoutCI = {
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout
    };

    VkPipelineLayout pipelineLayout =
        VK_NULL_HANDLE;

    r = vkCreatePipelineLayout(
        device,
        &pipelineLayoutCI,
        NULL,
        &pipelineLayout);

    printf("vkCreatePipelineLayout: %d\n", r);

    if (r != VK_SUCCESS)
        return 27;

    VkPipelineShaderStageCreateInfo shaderStage = {
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shaderModule,
        .pName = "main"
    };

    VkComputePipelineCreateInfo pipelineCI = {
        .sType =
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = shaderStage,
        .layout = pipelineLayout
    };

    VkPipeline pipeline = VK_NULL_HANDLE;

    r = vkCreateComputePipelines(
        device,
        VK_NULL_HANDLE,
        1,
        &pipelineCI,
        NULL,
        &pipeline);

    printf("vkCreateComputePipelines: %d\n", r);

    if (r != VK_SUCCESS)
        return 28;

    /*
     * Input image view.
     */
    VkImageViewCreateInfo inputViewCI = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = inputImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VkImageView inputView = VK_NULL_HANDLE;

    r = vkCreateImageView(
        device,
        &inputViewCI,
        NULL,
        &inputView);

    printf("vkCreateImageView(input): %d\n", r);

    if (r != VK_SUCCESS)
        return 29;

    /*
     * Output storage image view.
     */
    VkImageViewCreateInfo outputViewCI = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = outputImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VkImageView outputView = VK_NULL_HANDLE;

    r = vkCreateImageView(
        device,
        &outputViewCI,
        NULL,
        &outputView);

    printf("vkCreateImageView(output): %d\n", r);

    if (r != VK_SUCCESS)
        return 30;

    /*
     * Sampler.
     */
    VkSamplerCreateInfo samplerCI = {
        .sType =
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f
    };

    VkSampler sampler = VK_NULL_HANDLE;

    r = vkCreateSampler(
        device,
        &samplerCI,
        NULL,
        &sampler);

    printf("vkCreateSampler: %d\n", r);

    if (r != VK_SUCCESS)
        return 31;

    /*
     * Descriptor pool.
     */
    VkDescriptorPoolSize poolSizes[2] = {
        {
            .type =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1
        },
        {
            .type =
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1
        }
    };

    VkDescriptorPoolCreateInfo poolCI = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes
    };

    VkDescriptorPool descriptorPool =
        VK_NULL_HANDLE;

    r = vkCreateDescriptorPool(
        device,
        &poolCI,
        NULL,
        &descriptorPool);

    printf("vkCreateDescriptorPool: %d\n", r);

    if (r != VK_SUCCESS)
        return 32;

    VkDescriptorSetAllocateInfo setAI = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &setLayout
    };

    VkDescriptorSet descriptorSet =
        VK_NULL_HANDLE;

    r = vkAllocateDescriptorSets(
        device,
        &setAI,
        &descriptorSet);

    printf("vkAllocateDescriptorSets: %d\n", r);

    if (r != VK_SUCCESS)
        return 33;

    VkDescriptorImageInfo inputDescriptor = {
        .sampler = sampler,
        .imageView = inputView,
        .imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkDescriptorImageInfo outputDescriptor = {
        .imageView = outputView,
        .imageLayout =
            VK_IMAGE_LAYOUT_GENERAL
    };

    VkWriteDescriptorSet writes[2];

    memset(writes, 0, sizeof(writes));

    writes[0].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &inputDescriptor;

    writes[1].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &outputDescriptor;

    vkUpdateDescriptorSets(
        device,
        2,
        writes,
        0,
        NULL);

    /*
     * Command pool and buffer.
     */
    VkCommandPoolCreateInfo commandPoolCI = {
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = computeFamily
    };

    VkCommandPool commandPool =
        VK_NULL_HANDLE;

    r = vkCreateCommandPool(
        device,
        &commandPoolCI,
        NULL,
        &commandPool);

    printf("vkCreateCommandPool: %d\n", r);

    if (r != VK_SUCCESS)
        return 34;

    VkCommandBufferAllocateInfo commandBufferAI = {
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer commandBuffer =
        VK_NULL_HANDLE;

    r = vkAllocateCommandBuffers(
        device,
        &commandBufferAI,
        &commandBuffer);

    printf("vkAllocateCommandBuffers: %d\n", r);

    if (r != VK_SUCCESS)
        return 35;

    VkCommandBufferBeginInfo beginInfo = {
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };

    r = vkBeginCommandBuffer(
        commandBuffer,
        &beginInfo);

    printf("vkBeginCommandBuffer: %d\n", r);

    if (r != VK_SUCCESS)
        return 36;

    /*
     * 1. staging buffer -> input image
     */
    VkImageMemoryBarrier inputUndefinedToTransfer = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout =
            VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .image = inputImage,
        .subresourceRange = {
            .aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &inputUndefinedToTransfer);

    VkBufferImageCopy uploadRegion = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = {
            .x = 0,
            .y = 0,
            .z = 0
        },
        .imageExtent = extent
    };

    cmdCopyBufferToImage(
        commandBuffer,
        buffer,
        inputImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &uploadRegion);

    /*
     * 2. Input image -> shader-read layout.
     */
    VkImageMemoryBarrier inputToShaderRead = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT,
        .oldLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .image = inputImage,
        .subresourceRange = {
            .aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &inputToShaderRead);

    /*
     * 3. Output image -> GENERAL for imageStore().
     */
    VkImageMemoryBarrier outputToGeneral = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask =
            VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout =
            VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout =
            VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .image = outputImage,
        .subresourceRange = {
            .aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &outputToGeneral);

    /*
     * Compute pass.
     */
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline);

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        NULL);

    vkCmdDispatch(
        commandBuffer,
        8,
        8,
        1);

    /*
     * 4. Output image -> transfer source.
     */
    VkImageMemoryBarrier outputToTransfer = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask =
            VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask =
            VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout =
            VK_IMAGE_LAYOUT_GENERAL,
        .newLayout =
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .image = outputImage,
        .subresourceRange = {
            .aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &outputToTransfer);

    VkBufferImageCopy readbackRegion = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = {
            .x = 0,
            .y = 0,
            .z = 0
        },
        .imageExtent = extent
    };

    cmdCopyImageToBuffer(
        commandBuffer,
        outputImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        buffer,
        1,
        &readbackRegion);

    r = vkEndCommandBuffer(
        commandBuffer);

    printf("vkEndCommandBuffer: %d\n", r);

    if (r != VK_SUCCESS)
        return 37;

    /*
     * Submit and wait.
     */
    VkFenceCreateInfo fenceCI = {
        .sType =
            VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };

    VkFence fence = VK_NULL_HANDLE;

    r = vkCreateFence(
        device,
        &fenceCI,
        NULL,
        &fence);

    printf("vkCreateFence: %d\n", r);

    if (r != VK_SUCCESS)
        return 38;

    VkSubmitInfo submitInfo = {
        .sType =
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer
    };

    r = vkQueueSubmit(
        queue,
        1,
        &submitInfo,
        fence);

    printf("vkQueueSubmit: %d\n", r);

    if (r != VK_SUCCESS)
        return 39;

    r = vkWaitForFences(
        device,
        1,
        &fence,
        VK_TRUE,
        UINT64_MAX);

    printf("vkWaitForFences: %d\n", r);

    if (r != VK_SUCCESS)
        return 40;

    /*
     * Read back the output.
     */
    r = vkMapMemory(
        device,
        bufferMemory,
        0,
        bufferSize,
        0,
        (void **)&mapped);

    printf("vkMapMemory(readback): %d\n", r);

    if (r != VK_SUCCESS)
        return 41;

    struct TestPixel {
        unsigned x;
        unsigned y;
    };

    const struct TestPixel tests[] = {
        {0, 0},
        {63, 0},
        {0, 63},
        {63, 63},
        {32, 32},
        {16, 48}
    };

    unsigned failures = 0;

    for (unsigned i = 0;
         i < sizeof(tests) / sizeof(tests[0]);
         i++) {

        unsigned x = tests[i].x;
        unsigned y = tests[i].y;

        size_t offset =
            ((size_t)y * 64u + x) * 4u;

        uint8_t *p = mapped + offset;

        uint8_t expectedR =
            quantize_unorm(x);

        uint8_t expectedG =
            quantize_unorm(y);

        uint8_t expectedB = 64;
        uint8_t expectedA = 255;

        int ok =
            p[0] == expectedR &&
            p[1] == expectedG &&
            p[2] == expectedB &&
            p[3] == expectedA;

        printf(
            "pixel[%02u,%02u] = "
            "%02x %02x %02x %02x "
            "expected=%02x %02x %02x %02x %s\n",
            x,
            y,
            p[0],
            p[1],
            p[2],
            p[3],
            expectedR,
            expectedG,
            expectedB,
            expectedA,
            ok ? "OK" : "FAIL");

        if (!ok)
            failures++;
    }

    vkUnmapMemory(
        device,
        bufferMemory);

    printf("\n=== SAMPLED -> STORAGE RESULT ===\n");

    if (failures == 0) {
        printf("RESULT: PASS\n");
        printf(
            "SAMPLED IMAGE -> COMPUTE -> STORAGE IMAGE: YES\n");
        printf(
            "GPU texture sampling -> imageStore -> CPU readback: YES\n");
    } else {
        printf("RESULT: FAIL\n");
        printf("Failures: %u\n", failures);
        return 42;
    }

    vkDestroyFence(
        device,
        fence,
        NULL);

    vkDestroyCommandPool(
        device,
        commandPool,
        NULL);

    vkDestroyDescriptorPool(
        device,
        descriptorPool,
        NULL);

    vkDestroySampler(
        device,
        sampler,
        NULL);

    vkDestroyImageView(
        device,
        inputView,
        NULL);

    vkDestroyImageView(
        device,
        outputView,
        NULL);

    vkDestroyPipeline(
        device,
        pipeline,
        NULL);

    vkDestroyPipelineLayout(
        device,
        pipelineLayout,
        NULL);

    vkDestroyShaderModule(
        device,
        shaderModule,
        NULL);

    vkDestroyDescriptorSetLayout(
        device,
        setLayout,
        NULL);

    vkDestroyBuffer(
        device,
        buffer,
        NULL);

    vkDestroyImage(
        device,
        inputImage,
        NULL);

    vkDestroyImage(
        device,
        outputImage,
        NULL);

    vkFreeMemory(
        device,
        bufferMemory,
        NULL);

    vkFreeMemory(
        device,
        inputMemory,
        NULL);

    vkFreeMemory(
        device,
        outputMemory,
        NULL);

    vkDestroyDevice(
        device,
        NULL);

    return 0;
}
