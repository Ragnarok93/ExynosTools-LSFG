#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

static uint32_t find_memory_type(
    const VkPhysicalDeviceMemoryProperties *mem,
    uint32_t bits,
    VkMemoryPropertyFlags wanted)
{
    for (uint32_t i = 0; i < mem->memoryTypeCount; i++) {
        if ((bits & (1u << i)) &&
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

#define LOAD_DEVICE(name)                                      \
    PFN_##name name =                                          \
        (PFN_##name)getDeviceProcAddr(device, #name);       \
    if (!name) {                                                \
        printf("MISSING DEVICE FUNCTION: %s\n", #name);       \
        return 100;                                             \
    }

int main(void)
{
    const uint32_t W = 64;
    const uint32_t H = 64;
    const VkDeviceSize imageSize = W * H * 4;

    printf("=== XCLIPSE 940 STORAGE IMAGE EXECUTION PROBE ===\n");
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
        .pApplicationName = "LSFG Image Execution Probe",
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
        (PFN_vkEnumeratePhysicalDevices)getProc(
            instance,
            "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceProperties getProps =
        (PFN_vkGetPhysicalDeviceProperties)getProc(
            instance,
            "vkGetPhysicalDeviceProperties");

    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueues =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)getProc(
            instance,
            "vkGetPhysicalDeviceQueueFamilyProperties");

    PFN_vkGetPhysicalDeviceMemoryProperties getMemory =
        (PFN_vkGetPhysicalDeviceMemoryProperties)getProc(
            instance,
            "vkGetPhysicalDeviceMemoryProperties");

    PFN_vkCreateDevice createDevice =
        (PFN_vkCreateDevice)getProc(
            instance,
            "vkCreateDevice");

    PFN_vkGetDeviceProcAddr getDeviceProcAddr =
        (PFN_vkGetDeviceProcAddr)getProc(
            instance,
            "vkGetDeviceProcAddr");

    if (!enumerate ||
        !getProps ||
        !getQueues ||
        !getMemory ||
        !createDevice ||
        !getDeviceProcAddr) {
        printf("Required instance functions missing\n");
        return 4;
    }

    uint32_t physicalCount = 0;

    r = enumerate(instance, &physicalCount, NULL);

    if (r != VK_SUCCESS || physicalCount == 0) {
        printf("No physical devices: %d\n", r);
        return 5;
    }

    VkPhysicalDevice devices[8];

    if (physicalCount > 8)
        physicalCount = 8;

    r = enumerate(
        instance,
        &physicalCount,
        devices);

    if (r != VK_SUCCESS)
        return 6;

    VkPhysicalDevice physical = devices[0];

    VkPhysicalDeviceProperties props;

    getProps(physical, &props);

    printf("GPU: %s\n", props.deviceName);
    printf(
        "Vulkan: %u.%u.%u\n",
        VK_VERSION_MAJOR(props.apiVersion),
        VK_VERSION_MINOR(props.apiVersion),
        VK_VERSION_PATCH(props.apiVersion));

    uint32_t queueCount = 0;

    getQueues(
        physical,
        &queueCount,
        NULL);

    VkQueueFamilyProperties queues[8];

    if (queueCount > 8)
        queueCount = 8;

    getQueues(
        physical,
        &queueCount,
        queues);

    uint32_t computeFamily = UINT32_MAX;

    for (uint32_t i = 0; i < queueCount; i++) {
        if ((queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            computeFamily = i;
            break;
        }
    }

    if (computeFamily == UINT32_MAX) {
        for (uint32_t i = 0; i < queueCount; i++) {
            if ((queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                computeFamily = i;
                break;
            }
        }
    }

    if (computeFamily == UINT32_MAX) {
        printf("No compute queue\n");
        return 7;
    }

    printf(
        "Selected compute queue family: %u\n",
        computeFamily);

    float priority = 1.0f;

    VkDeviceQueueCreateInfo queueCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = computeFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority
    };

    VkDeviceCreateInfo deviceCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
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
        return 8;

    LOAD_DEVICE(vkGetDeviceQueue);
    LOAD_DEVICE(vkCreateImage);
    LOAD_DEVICE(vkGetImageMemoryRequirements);
    LOAD_DEVICE(vkAllocateMemory);
    LOAD_DEVICE(vkBindImageMemory);
    LOAD_DEVICE(vkCreateBuffer);
    LOAD_DEVICE(vkGetBufferMemoryRequirements);
    LOAD_DEVICE(vkBindBufferMemory);
    LOAD_DEVICE(vkMapMemory);
    LOAD_DEVICE(vkUnmapMemory);
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
    LOAD_DEVICE(vkCmdPipelineBarrier);
    LOAD_DEVICE(vkCmdBindPipeline);
    LOAD_DEVICE(vkCmdBindDescriptorSets);
    LOAD_DEVICE(vkCmdDispatch);
    LOAD_DEVICE(vkCmdCopyImageToBuffer);
    LOAD_DEVICE(vkEndCommandBuffer);
    LOAD_DEVICE(vkCreateFence);
    LOAD_DEVICE(vkQueueSubmit);
    LOAD_DEVICE(vkWaitForFences);
    LOAD_DEVICE(vkDestroyFence);
    LOAD_DEVICE(vkDestroyCommandPool);
    LOAD_DEVICE(vkDestroyDescriptorPool);
    LOAD_DEVICE(vkDestroyPipeline);
    LOAD_DEVICE(vkDestroyPipelineLayout);
    LOAD_DEVICE(vkDestroyShaderModule);
    LOAD_DEVICE(vkDestroyDescriptorSetLayout);
    LOAD_DEVICE(vkDestroyImage);
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
        return 9;
    }

    /*
     * GPU storage image.
     */
    VkImageCreateInfo imageCI = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = W,
            .height = H,
            .depth = 1
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage =
            VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VkImage image = VK_NULL_HANDLE;

    r = vkCreateImage(
        device,
        &imageCI,
        NULL,
        &image);

    printf("vkCreateImage: %d\n", r);

    if (r != VK_SUCCESS)
        return 10;

    VkMemoryRequirements imageReq;

    vkGetImageMemoryRequirements(
        device,
        image,
        &imageReq);

    VkPhysicalDeviceMemoryProperties memProps;

    getMemory(
        physical,
        &memProps);

    uint32_t imageMemoryType =
        find_memory_type(
            &memProps,
            imageReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (imageMemoryType == UINT32_MAX) {
        printf("No device-local image memory type\n");
        return 11;
    }

    VkMemoryAllocateInfo imageAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = imageReq.size,
        .memoryTypeIndex = imageMemoryType
    };

    VkDeviceMemory imageMemory = VK_NULL_HANDLE;

    r = vkAllocateMemory(
        device,
        &imageAlloc,
        NULL,
        &imageMemory);

    printf("vkAllocateMemory(image): %d\n", r);

    if (r != VK_SUCCESS)
        return 12;

    r = vkBindImageMemory(
        device,
        image,
        imageMemory,
        0);

    printf("vkBindImageMemory: %d\n", r);

    if (r != VK_SUCCESS)
        return 13;

    /*
     * Host-visible readback buffer.
     */
    VkBufferCreateInfo bufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = imageSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkBuffer buffer = VK_NULL_HANDLE;

    r = vkCreateBuffer(
        device,
        &bufferCI,
        NULL,
        &buffer);

    printf("vkCreateBuffer(readback): %d\n", r);

    if (r != VK_SUCCESS)
        return 14;

    VkMemoryRequirements bufferReq;

    vkGetBufferMemoryRequirements(
        device,
        buffer,
        &bufferReq);

    uint32_t bufferMemoryType =
        find_memory_type(
            &memProps,
            bufferReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (bufferMemoryType == UINT32_MAX) {
        printf("No host-visible coherent buffer memory\n");
        return 15;
    }

    VkMemoryAllocateInfo bufferAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = bufferReq.size,
        .memoryTypeIndex = bufferMemoryType
    };

    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;

    r = vkAllocateMemory(
        device,
        &bufferAlloc,
        NULL,
        &bufferMemory);

    printf("vkAllocateMemory(buffer): %d\n", r);

    if (r != VK_SUCCESS)
        return 16;

    r = vkBindBufferMemory(
        device,
        buffer,
        bufferMemory,
        0);

    printf("vkBindBufferMemory: %d\n", r);

    if (r != VK_SUCCESS)
        return 17;

    /*
     * Load image shader.
     */
    uint32_t *spirv = NULL;
    size_t spirvSize = 0;

    int loadResult = load_spirv(
        "/data/data/com.termux/files/home/ExynosTools-LSFG/lsfg_image_probe.spv",
        &spirv,
        &spirvSize);

    if (loadResult != 0) {
        printf("Failed to load SPIR-V: %d\n", loadResult);
        return 18;
    }

    VkShaderModuleCreateInfo shaderCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirvSize,
        .pCode = spirv
    };

    VkShaderModule shader = VK_NULL_HANDLE;

    r = vkCreateShaderModule(
        device,
        &shaderCI,
        NULL,
        &shader);

    free(spirv);

    printf("vkCreateShaderModule: %d\n", r);

    if (r != VK_SUCCESS)
        return 19;

    /*
     * Storage image descriptor.
     */
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };

    VkDescriptorSetLayoutCreateInfo setLayoutCI = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;

    r = vkCreateDescriptorSetLayout(
        device,
        &setLayoutCI,
        NULL,
        &setLayout);

    printf("vkCreateDescriptorSetLayout: %d\n", r);

    if (r != VK_SUCCESS)
        return 20;

    VkPipelineLayoutCreateInfo pipelineLayoutCI = {
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout
    };

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    r = vkCreatePipelineLayout(
        device,
        &pipelineLayoutCI,
        NULL,
        &pipelineLayout);

    printf("vkCreatePipelineLayout: %d\n", r);

    if (r != VK_SUCCESS)
        return 21;

    VkPipelineShaderStageCreateInfo stage = {
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shader,
        .pName = "main"
    };

    VkComputePipelineCreateInfo pipelineCI = {
        .sType =
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stage,
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
        return 22;

    VkDescriptorPoolSize poolSize = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1
    };

    VkDescriptorPoolCreateInfo poolCI = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };

    VkDescriptorPool pool = VK_NULL_HANDLE;

    r = vkCreateDescriptorPool(
        device,
        &poolCI,
        NULL,
        &pool);

    printf("vkCreateDescriptorPool: %d\n", r);

    if (r != VK_SUCCESS)
        return 23;

    VkDescriptorSetAllocateInfo setAI = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &setLayout
    };

    VkDescriptorSet set = VK_NULL_HANDLE;

    r = vkAllocateDescriptorSets(
        device,
        &setAI,
        &set);

    printf("vkAllocateDescriptorSets: %d\n", r);

    if (r != VK_SUCCESS)
        return 24;

    /*
     * The image is bound as GENERAL because the compute shader
     * performs imageStore().
     */
    VkDescriptorImageInfo imageInfo = {
        .sampler = VK_NULL_HANDLE,
        .imageView = VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    /*
     * Need an image view for the descriptor.
     */
    PFN_vkCreateImageView vkCreateImageView =
        (PFN_vkCreateImageView)getDeviceProcAddr(
            device,
            "vkCreateImageView");

    PFN_vkDestroyImageView vkDestroyImageView =
        (PFN_vkDestroyImageView)getDeviceProcAddr(
            device,
            "vkDestroyImageView");

    if (!vkCreateImageView || !vkDestroyImageView) {
        printf("Image view functions missing\n");
        return 25;
    }

    VkImageViewCreateInfo viewCI = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
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

    VkImageView view = VK_NULL_HANDLE;

    r = vkCreateImageView(
        device,
        &viewCI,
        NULL,
        &view);

    printf("vkCreateImageView: %d\n", r);

    if (r != VK_SUCCESS)
        return 26;

    imageInfo.imageView = view;

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfo
    };

    vkUpdateDescriptorSets(
        device,
        1,
        &write,
        0,
        NULL);

    VkCommandPoolCreateInfo commandPoolCI = {
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = computeFamily
    };

    VkCommandPool commandPool = VK_NULL_HANDLE;

    r = vkCreateCommandPool(
        device,
        &commandPoolCI,
        NULL,
        &commandPool);

    printf("vkCreateCommandPool: %d\n", r);

    if (r != VK_SUCCESS)
        return 27;

    VkCommandBufferAllocateInfo commandBufferAI = {
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    r = vkAllocateCommandBuffers(
        device,
        &commandBufferAI,
        &commandBuffer);

    printf("vkAllocateCommandBuffers: %d\n", r);

    if (r != VK_SUCCESS)
        return 28;

    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };

    r = vkBeginCommandBuffer(
        commandBuffer,
        &begin);

    printf("vkBeginCommandBuffer: %d\n", r);

    if (r != VK_SUCCESS)
        return 29;

    /*
     * UNDEFINED -> GENERAL
     */
    VkImageMemoryBarrier imageToGeneral = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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
        &imageToGeneral);

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
        &set,
        0,
        NULL);

    /*
     * Shader uses 8x8 local size and image is 64x64.
     */
    vkCmdDispatch(
        commandBuffer,
        8,
        8,
        1);

    /*
     * GENERAL -> TRANSFER_SRC_OPTIMAL.
     */
    VkImageMemoryBarrier imageToTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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
        &imageToTransfer);

    VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = {
            .x = 0,
            .y = 0,
            .z = 0
        },
        .imageExtent = {
            .width = W,
            .height = H,
            .depth = 1
        }
    };

    vkCmdCopyImageToBuffer(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        buffer,
        1,
        &copy);

    VkBufferMemoryBarrier bufferBarrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = 0,
        .size = imageSize
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        NULL,
        1,
        &bufferBarrier,
        0,
        NULL);

    r = vkEndCommandBuffer(commandBuffer);

    printf("vkEndCommandBuffer: %d\n", r);

    if (r != VK_SUCCESS)
        return 30;

    VkFenceCreateInfo fenceCI = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };

    VkFence fence = VK_NULL_HANDLE;

    r = vkCreateFence(
        device,
        &fenceCI,
        NULL,
        &fence);

    printf("vkCreateFence: %d\n", r);

    if (r != VK_SUCCESS)
        return 31;

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer
    };

    r = vkQueueSubmit(
        queue,
        1,
        &submit,
        fence);

    printf("vkQueueSubmit: %d\n", r);

    if (r != VK_SUCCESS)
        return 32;

    r = vkWaitForFences(
        device,
        1,
        &fence,
        VK_TRUE,
        UINT64_MAX);

    printf("vkWaitForFences: %d\n", r);

    if (r != VK_SUCCESS)
        return 33;

    uint8_t *pixels = NULL;

    r = vkMapMemory(
        device,
        bufferMemory,
        0,
        imageSize,
        0,
        (void **)&pixels);

    printf("vkMapMemory(readback): %d\n", r);

    if (r != VK_SUCCESS)
        return 34;

    /*
     * Validate recognizable pixels.
     */
    struct TestPixel {
        uint32_t x;
        uint32_t y;
    } tests[] = {
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

        uint32_t x = tests[i].x;
        uint32_t y = tests[i].y;

        size_t offset =
            ((size_t)y * W + x) * 4;

        uint8_t *p = pixels + offset;

        uint8_t expectedR =
            (uint8_t)((x * 255u + 31u) / 63u);

        uint8_t expectedG =
            (uint8_t)((y * 255u + 31u) / 63u);

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
            p[0], p[1], p[2], p[3],
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

    vkDestroyFence(device, fence, NULL);
    vkDestroyImageView(device, view, NULL);
    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyDescriptorPool(device, pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyShaderModule(device, shader, NULL);
    vkDestroyDescriptorSetLayout(device, setLayout, NULL);
    vkDestroyImage(device, image, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, imageMemory, NULL);
    vkFreeMemory(device, bufferMemory, NULL);
    vkDestroyDevice(device, NULL);

    printf("\n=== IMAGE EXECUTION RESULT ===\n");

    if (failures == 0) {
        printf("RESULT: PASS\n");
        printf("STORAGE IMAGE COMPUTE EXECUTION: YES\n");
        printf("GPU imageStore -> transfer -> CPU readback: YES\n");
        return 0;
    }

    printf("RESULT: FAIL\n");
    printf("Failures: %u\n", failures);

    return 40;
}
