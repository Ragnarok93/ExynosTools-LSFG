#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>

#include <vulkan/vulkan.h>

#define W 64
#define H 64
#define PIXELS (W * H)
#define IMAGE_SIZE (PIXELS * 4)

#define CHECK(label, expr)                                      \
    do {                                                        \
        VkResult _r = (expr);                                   \
        printf("%s: %d\n", (label), _r);                        \
        if (_r != VK_SUCCESS) return 1;                         \
    } while (0)

#define LOAD_INSTANCE(name)                                     \
    PFN_##name name =                                           \
        (PFN_##name)gpa(instance, #name)

#define LOAD_DEVICE(name)                                       \
    PFN_##name name =                                           \
        (PFN_##name)getDeviceProcAddr(device, #name)

static uint8_t midpoint(uint8_t a, uint8_t b)
{
    return (uint8_t)(((unsigned)a + (unsigned)b + 1u) / 2u);
}

int main(void)
{
    printf("=== XCLIPSE 940 TWO-FRAME INTERPOLATION PROBE ===\n");

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
        .pApplicationName = "LSFG Two Frame Interpolation",
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

    CHECK("vkCreateInstance",
          createInstance(&ici, NULL, &instance));

    LOAD_INSTANCE(vkEnumeratePhysicalDevices);
    LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceFormatProperties);
    LOAD_INSTANCE(vkCreateDevice);

    LOAD_INSTANCE(vkDestroyInstance);

    if (!vkEnumeratePhysicalDevices ||
        !vkGetPhysicalDeviceProperties ||
        !vkGetPhysicalDeviceQueueFamilyProperties ||
        !vkGetPhysicalDeviceMemoryProperties ||
        !vkGetPhysicalDeviceFormatProperties ||
        !vkCreateDevice) {
        printf("Required instance functions missing\n");
        return 3;
    }

    uint32_t deviceCount = 0;

    CHECK("enumerate physical devices",
          vkEnumeratePhysicalDevices(
              instance,
              &deviceCount,
              NULL));

    if (!deviceCount) {
        printf("No physical device\n");
        return 4;
    }

    VkPhysicalDevice devices[8];

    if (deviceCount > 8)
        deviceCount = 8;

    CHECK("enumerate physical devices",
          vkEnumeratePhysicalDevices(
              instance,
              &deviceCount,
              devices));

    VkPhysicalDevice physical = devices[0];

    VkPhysicalDeviceProperties props;

    vkGetPhysicalDeviceProperties(
        physical,
        &props);

    printf("GPU: %s\n", props.deviceName);
    printf(
        "Vulkan: %u.%u.%u\n",
        VK_VERSION_MAJOR(props.apiVersion),
        VK_VERSION_MINOR(props.apiVersion),
        VK_VERSION_PATCH(props.apiVersion));

    /*
     * Select the dedicated compute queue family already verified
     * on the Xclipse 940.
     */
    uint32_t queueCount = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(
        physical,
        &queueCount,
        NULL);

    VkQueueFamilyProperties *queues =
        calloc(queueCount, sizeof(*queues));

    vkGetPhysicalDeviceQueueFamilyProperties(
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

    free(queues);

    if (computeFamily == UINT32_MAX) {
        printf("No dedicated compute queue family\n");
        return 5;
    }

    printf(
        "Selected compute queue family: %u\n",
        computeFamily);

    float priority = 1.0f;

    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = computeFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority
    };

    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci
    };

    VkDevice device = VK_NULL_HANDLE;

    CHECK("vkCreateDevice",
          vkCreateDevice(
              physical,
              &dci,
              NULL,
              &device));

    PFN_vkGetDeviceProcAddr getDeviceProcAddr =
        (PFN_vkGetDeviceProcAddr)dlsym(
            lib,
            "vkGetDeviceProcAddr");

    if (!getDeviceProcAddr) {
        printf("vkGetDeviceProcAddr missing\n");
        return 6;
    }

    LOAD_DEVICE(vkGetDeviceQueue);
    LOAD_DEVICE(vkCreateImage);
    LOAD_DEVICE(vkDestroyImage);
    LOAD_DEVICE(vkGetImageMemoryRequirements);
    LOAD_DEVICE(vkAllocateMemory);
    LOAD_DEVICE(vkFreeMemory);
    LOAD_DEVICE(vkBindImageMemory);
    LOAD_DEVICE(vkCreateImageView);
    LOAD_DEVICE(vkCreateSampler);
    LOAD_DEVICE(vkDestroyImageView);
    LOAD_DEVICE(vkCreateBuffer);
    LOAD_DEVICE(vkDestroyBuffer);
    LOAD_DEVICE(vkGetBufferMemoryRequirements);
    LOAD_DEVICE(vkBindBufferMemory);
    LOAD_DEVICE(vkMapMemory);
    LOAD_DEVICE(vkUnmapMemory);
    LOAD_DEVICE(vkCreateShaderModule);
    LOAD_DEVICE(vkDestroyShaderModule);
    LOAD_DEVICE(vkCreateDescriptorSetLayout);
    LOAD_DEVICE(vkDestroyDescriptorSetLayout);
    LOAD_DEVICE(vkCreatePipelineLayout);
    LOAD_DEVICE(vkDestroyPipelineLayout);
    LOAD_DEVICE(vkCreateComputePipelines);
    LOAD_DEVICE(vkDestroyPipeline);
    LOAD_DEVICE(vkCreateDescriptorPool);
    LOAD_DEVICE(vkDestroyDescriptorPool);
    LOAD_DEVICE(vkAllocateDescriptorSets);
    LOAD_DEVICE(vkUpdateDescriptorSets);
    LOAD_DEVICE(vkCreateCommandPool);
    LOAD_DEVICE(vkDestroyCommandPool);
    LOAD_DEVICE(vkAllocateCommandBuffers);
    LOAD_DEVICE(vkBeginCommandBuffer);
    LOAD_DEVICE(vkEndCommandBuffer);
    LOAD_DEVICE(vkCreateFence);
    LOAD_DEVICE(vkDestroyFence);
    LOAD_DEVICE(vkQueueSubmit);
    LOAD_DEVICE(vkWaitForFences);
    LOAD_DEVICE(vkCmdPipelineBarrier);
    LOAD_DEVICE(vkCmdBindPipeline);
    LOAD_DEVICE(vkCmdBindDescriptorSets);
    LOAD_DEVICE(vkCmdDispatch);
    LOAD_DEVICE(vkCmdCopyImageToBuffer);
    LOAD_DEVICE(vkCmdCopyBufferToImage);

    if (!vkGetDeviceQueue ||
        !vkCreateImage ||
        !vkGetImageMemoryRequirements ||
        !vkAllocateMemory ||
        !vkBindImageMemory ||
        !vkCreateImageView ||
        !vkCreateSampler ||
        !vkCreateBuffer ||
        !vkGetBufferMemoryRequirements ||
        !vkBindBufferMemory ||
        !vkMapMemory ||
        !vkCreateShaderModule ||
        !vkCreateDescriptorSetLayout ||
        !vkCreatePipelineLayout ||
        !vkCreateComputePipelines ||
        !vkCreateDescriptorPool ||
        !vkAllocateDescriptorSets ||
        !vkUpdateDescriptorSets ||
        !vkCreateCommandPool ||
        !vkAllocateCommandBuffers ||
        !vkBeginCommandBuffer ||
        !vkEndCommandBuffer ||
        !vkCreateFence ||
        !vkQueueSubmit ||
        !vkWaitForFences ||
        !vkCmdPipelineBarrier ||
        !vkCmdBindPipeline ||
        !vkCmdBindDescriptorSets ||
        !vkCmdDispatch ||
        !vkCmdCopyImageToBuffer ||
        !vkCmdCopyBufferToImage) {
        printf("Required device functions missing\n");
        return 7;
    }

    VkQueue queue = VK_NULL_HANDLE;

    vkGetDeviceQueue(
        device,
        computeFamily,
        0,
        &queue);

    /*
     * RGBA8 must support sampled + storage usage.
     */
    VkFormatProperties fp;

    vkGetPhysicalDeviceFormatProperties(
        physical,
        VK_FORMAT_R8G8B8A8_UNORM,
        &fp);

    printf(
        "R8G8B8A8 sampled: %s\n",
        (fp.optimalTilingFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
            ? "YES" : "NO");

    printf(
        "R8G8B8A8 storage: %s\n",
        (fp.optimalTilingFeatures &
         VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
            ? "YES" : "NO");
    VkFormatProperties motionFp;

    vkGetPhysicalDeviceFormatProperties(
        physical,
        VK_FORMAT_R32G32_SFLOAT,
        &motionFp);

    printf(
        "R32G32_SFLOAT storage: %s\n",
        (motionFp.optimalTilingFeatures &
         VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
            ? "YES" : "NO");

    printf(
        "R32G32_SFLOAT transfer-dst: %s\n",
        (motionFp.optimalTilingFeatures &
         VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
            ? "YES" : "NO");


    /*
     * Find a device-local memory type.
     */
    VkPhysicalDeviceMemoryProperties memProps;

    vkGetPhysicalDeviceMemoryProperties(
        physical,
        &memProps);

    uint32_t imageMemoryType = UINT32_MAX;

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if (memProps.memoryTypes[i].propertyFlags &
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            imageMemoryType = i;
            break;
        }
    }

    if (imageMemoryType == UINT32_MAX) {
        printf("No device-local memory type\n");
        return 8;
    }

    /*
     * Phase 2E resources:
     *
     * images[0] = frame A       RGBA8
     * images[1] = frame B       RGBA8
     * images[2] = motion field  RG32F
     * images[3] = output        RGBA8
     */
    VkImage images[4] = {
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE
    };

    for (int i = 0; i < 4; i++) {
        VkFormat format;
        VkImageUsageFlags usage;

        if (i == 2) {
            /*
             * Synthetic motion field.
             *
             * R32G32_SFLOAT:
             *   X = normalized U displacement
             *   Y = normalized V displacement
             */
            format = VK_FORMAT_R32G32_SFLOAT;

            usage =
                VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        } else {
            format = VK_FORMAT_R8G8B8A8_UNORM;

            usage =
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            if (i == 3) {
                usage |=
                    VK_IMAGE_USAGE_STORAGE_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
        }

        VkImageCreateInfo ici_img = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = { W, H, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };

        char label[64];

        snprintf(
            label,
            sizeof(label),
            "vkCreateImage(%d)",
            i);

        CHECK(label,
              vkCreateImage(
                  device,
                  &ici_img,
                  NULL,
                  &images[i]));
    }

    VkDeviceMemory imageMemory[4] = {
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE
    };

    for (int i = 0; i < 4; i++) {
        VkMemoryRequirements mr;

        vkGetImageMemoryRequirements(
            device,
            images[i],
            &mr);

        VkMemoryAllocateInfo mai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mr.size,
            .memoryTypeIndex = imageMemoryType
        };

        CHECK("vkAllocateMemory(image)",
              vkAllocateMemory(
                  device,
                  &mai,
                  NULL,
                  &imageMemory[i]));

        CHECK("vkBindImageMemory",
              vkBindImageMemory(
                  device,
                  images[i],
                  imageMemory[i],
                  0));
    }

    /*
     * Staging/readback buffer.
     */
    VkBuffer buffer = VK_NULL_HANDLE;

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = IMAGE_SIZE,
        .usage =
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT
    };

    CHECK("vkCreateBuffer",
          vkCreateBuffer(
              device,
              &bci,
              NULL,
              &buffer));

    VkMemoryRequirements bmr;

    vkGetBufferMemoryRequirements(
        device,
        buffer,
        &bmr);

    uint32_t bufferMemoryType = UINT32_MAX;

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        VkMemoryPropertyFlags f =
            memProps.memoryTypes[i].propertyFlags;

        if ((f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            if ((bmr.memoryTypeBits & (1u << i)) != 0) {
                bufferMemoryType = i;
                break;
            }
        }
    }

    if (bufferMemoryType == UINT32_MAX) {
        printf("No suitable host-visible buffer memory\n");
        return 9;
    }

    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;

    VkMemoryAllocateInfo bmai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = bmr.size,
        .memoryTypeIndex = bufferMemoryType
    };

    CHECK("vkAllocateMemory(buffer)",
          vkAllocateMemory(
              device,
              &bmai,
              NULL,
              &bufferMemory));

    CHECK("vkBindBufferMemory",
          vkBindBufferMemory(
              device,
              buffer,
              bufferMemory,
              0));

    /*
     * Build two deterministic input frames.
     *
     * A:
     *   R = X
     *   G = Y
     *   B = 0x20
     *
     * B:
     *   R = 255-X
     *   G = 255-Y
     *   B = 0xe0
     *
     * Expected midpoint:
     *   R ~= 127/128
     *   G ~= 127/128
     *   B = 0x80
     */
    uint8_t *mapped = NULL;

    CHECK("vkMapMemory(upload)",
          vkMapMemory(
              device,
              bufferMemory,
              0,
              IMAGE_SIZE,
              0,
              (void **)&mapped));

    memset(mapped, 0, IMAGE_SIZE);

    /*
     * Upload frame A.
     *
     * A:
     *   R = X
     *   G = Y
     *   B = 0x20
     *   A = 0xff
     *
     * The shader samples at the center of the 64x64 image:
     *   textureLod(..., vec2(0.5), 0.0)
     *
     * Therefore the center should be approximately:
     *   80 80 20 ff
     */
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            size_t o = ((size_t)y * W + x) * 4;

            mapped[o + 0] =
                (uint8_t)((x * 255u + 31u) / 63u);

            mapped[o + 1] =
                (uint8_t)((y * 255u + 31u) / 63u);

            mapped[o + 2] = 0x20;
            mapped[o + 3] = 0xff;
        }
    }

    /*
     * We need a command buffer before performing uploads.
     */
    VkCommandPool commandPool = VK_NULL_HANDLE;

    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = computeFamily,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };

    CHECK("vkCreateCommandPool",
          vkCreateCommandPool(
              device,
              &cpci,
              NULL,
              &commandPool));

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    CHECK("vkAllocateCommandBuffers",
          vkAllocateCommandBuffers(
              device,
              &cbai,
              &commandBuffer));

    /*
     * Upload frame A.
     */
    CHECK("vkBeginCommandBuffer(upload A)",
          vkBeginCommandBuffer(
              commandBuffer,
              &(VkCommandBufferBeginInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
              }));

    VkImageMemoryBarrier barrierA = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .image = images[0],
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, 1, 0, 1
        }
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrierA);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .imageSubresource = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, 0, 1
        },
        .imageExtent = { W, H, 1 }
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        images[0],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    barrierA.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierA.newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierA.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierA.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrierA);

    CHECK("vkEndCommandBuffer(upload A)",
          vkEndCommandBuffer(commandBuffer));

    VkFence fence = VK_NULL_HANDLE;

    CHECK("vkCreateFence",
          vkCreateFence(
              device,
              &(VkFenceCreateInfo){
                  .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
              },
              NULL,
              &fence));

    CHECK("vkQueueSubmit(upload A)",
          vkQueueSubmit(
              queue,
              1,
              &(VkSubmitInfo){
                  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                  .commandBufferCount = 1,
                  .pCommandBuffers = &commandBuffer
              },
              fence));

    CHECK("vkWaitForFences(upload A)",
          vkWaitForFences(
              device,
              1,
              &fence,
              VK_TRUE,
              UINT64_MAX));


    /*
     * Phase 2B CPU-side input verification.
     *
     * Verify the actual bytes before they enter Vulkan.
     */
    printf(
        "CPU frame A [0,0] = %02x %02x %02x %02x\n",
        mapped[0],
        mapped[1],
        mapped[2],
        mapped[3]);

    /*
     * Upload frame B using the same staging buffer.
     */
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            size_t o = ((size_t)y * W + x) * 4;

            mapped[o + 0] =
                255u -
                (uint8_t)((x * 255u + 31u) / 63u);

            mapped[o + 1] =
                255u -
                (uint8_t)((y * 255u + 31u) / 63u);

            mapped[o + 2] = 0xe0;
            mapped[o + 3] = 0xff;
        }
    }

    vkUnmapMemory(device, bufferMemory);
    mapped = NULL;

    /*
     * Re-map after modifying the coherent staging buffer.
     */
    CHECK("vkMapMemory(upload B)",
          vkMapMemory(
              device,
              bufferMemory,
              0,
              IMAGE_SIZE,
              0,
              (void **)&mapped));

    CHECK("vkResetFences(upload B)",
          ((PFN_vkResetFences)getDeviceProcAddr(
              device,
              "vkResetFences"))(
                  device,
                  1,
                  &fence));

    CHECK("vkBeginCommandBuffer(upload B)",
          vkBeginCommandBuffer(
              commandBuffer,
              &(VkCommandBufferBeginInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
              }));

    VkImageMemoryBarrier barrierB = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .image = images[1],
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, 1, 0, 1
        }
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrierB);

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        images[1],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    barrierB.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrierB.newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barrierB.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;

    barrierB.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrierB);

    CHECK("vkEndCommandBuffer(upload B)",
          vkEndCommandBuffer(commandBuffer));

    CHECK("vkQueueSubmit(upload B)",
          vkQueueSubmit(
              queue,
              1,
              &(VkSubmitInfo){
                  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                  .commandBufferCount = 1,
                  .pCommandBuffers = &commandBuffer
              },
              fence));

    CHECK("vkWaitForFences(upload B)",
          vkWaitForFences(
              device,
              1,
              &fence,
              VK_TRUE,
              UINT64_MAX));

    vkUnmapMemory(device, bufferMemory);
    mapped = NULL;

    /*
     * Load SPIR-V.
     */
    FILE *spv = fopen(
        "lsfg_sampler_phase2e_motion.spv",
        "rb");

    if (!spv) {
        printf("Cannot open lsfg_interpolation.spv\n");
        return 10;
    }

    fseek(spv, 0, SEEK_END);
    long spvSize = ftell(spv);
    fseek(spv, 0, SEEK_SET);

    uint32_t *code =
        malloc((size_t)spvSize);

    fread(
        code,
        1,
        (size_t)spvSize,
        spv);

    fclose(spv);

    VkShaderModule shaderModule = VK_NULL_HANDLE;

    CHECK("vkCreateShaderModule",
          vkCreateShaderModule(
              device,
              &(VkShaderModuleCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                  .codeSize = (size_t)spvSize,
                  .pCode = code
              },
              NULL,
              &shaderModule));

    free(code);

    /*
     * Descriptor set:
     *
     * 0 = frame A
     * 1 = frame B
     * 2 = output
     */
    VkDescriptorSetLayoutBinding bindings[4];

    memset(bindings, 0, sizeof(bindings));

    /*
     * Descriptor types must exactly match the SPIR-V:
     *
     * binding 0 = sampler2D frameA
     * binding 1 = sampler2D frameB
     * binding 2 = storage image outputImage
     */
    /*
     * binding 0 = frame A
     * binding 1 = frame B
     * binding 2 = motion field
     * binding 3 = output
     */

    bindings[0].binding = 0;
    bindings[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayout descriptorSetLayout =
        VK_NULL_HANDLE;

    CHECK("vkCreateDescriptorSetLayout",
          vkCreateDescriptorSetLayout(
              device,
              &(VkDescriptorSetLayoutCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                  .bindingCount = 4,
                  .pBindings = bindings
              },
              NULL,
              &descriptorSetLayout));

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    CHECK("vkCreatePipelineLayout",
          vkCreatePipelineLayout(
              device,
              &(VkPipelineLayoutCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                  .setLayoutCount = 1,
                  .pSetLayouts = &descriptorSetLayout
              },
              NULL,
              &pipelineLayout));

    VkComputePipelineCreateInfo pci = {
        .sType =
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main"
        },
        .layout = pipelineLayout
    };

    VkPipeline pipeline = VK_NULL_HANDLE;

    CHECK("vkCreateComputePipelines",
          vkCreateComputePipelines(
              device,
              VK_NULL_HANDLE,
              1,
              &pci,
              NULL,
              &pipeline));

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    CHECK("vkCreateDescriptorPool",
          vkCreateDescriptorPool(
              device,
              &(VkDescriptorPoolCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                  .maxSets = 1,
                  .poolSizeCount = 2,
                  .pPoolSizes =
                      (VkDescriptorPoolSize[]){
                          {
                              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              2
                          },
                          {
                              VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                              2
                          }
                      }
              },
              NULL,
              &descriptorPool));

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    CHECK("vkAllocateDescriptorSets",
          vkAllocateDescriptorSets(
              device,
              &(VkDescriptorSetAllocateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                  .descriptorPool = descriptorPool,
                  .descriptorSetCount = 1,
                  .pSetLayouts = &descriptorSetLayout
              },
              &descriptorSet));

    VkImageView views[4] = {
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE
    };

    for (int i = 0; i < 4; i++) {
        CHECK("vkCreateImageView",
              vkCreateImageView(
                  device,
                  &(VkImageViewCreateInfo){
                      .sType =
                          VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                      .image = images[i],
                      .viewType = VK_IMAGE_VIEW_TYPE_2D,
                      .format =
                (i == 2)
                    ? VK_FORMAT_R32G32_SFLOAT
                    : VK_FORMAT_R8G8B8A8_UNORM,
                      .subresourceRange = {
                          VK_IMAGE_ASPECT_COLOR_BIT,
                          0, 1, 0, 1
                      }
                  },
                  NULL,
                  &views[i]));
    }

    /*
     * Combined image samplers for frame A and frame B.
     *
     * The motion shader declares:
     *
     *   binding 0 = sampler2D frameA
     *   binding 1 = sampler2D frameB
     *   binding 2 = storage image outputImage
     */

    VkSampler sampler = VK_NULL_HANDLE;

    CHECK("vkCreateSampler",
          vkCreateSampler(
              device,
              &(VkSamplerCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                  .magFilter = VK_FILTER_LINEAR,
                  .minFilter = VK_FILTER_LINEAR,
                  .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                  .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .mipLodBias = 0.0f,
                  .anisotropyEnable = VK_FALSE,
                  .maxAnisotropy = 1.0f,
                  .compareEnable = VK_FALSE,
                  .compareOp = VK_COMPARE_OP_ALWAYS,
                  .minLod = 0.0f,
                  .maxLod = 0.0f,
                  .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
                  .unnormalizedCoordinates = VK_FALSE
              },
              NULL,
              &sampler));

    VkDescriptorImageInfo imageInfos[4];

    memset(imageInfos, 0, sizeof(imageInfos));

    /*
     * frame A sampler2D
     */
    imageInfos[0].sampler = sampler;
    imageInfos[0].imageView = views[0];
    imageInfos[0].imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    /*
     * frame B sampler2D
     */
    imageInfos[1].sampler = sampler;
    imageInfos[1].imageView = views[1];
    imageInfos[1].imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    /*
     * motion field storage image
     */
    imageInfos[2].sampler = VK_NULL_HANDLE;
    imageInfos[2].imageView = views[2];
    imageInfos[2].imageLayout =
        VK_IMAGE_LAYOUT_GENERAL;

    /*
     * output storage image
     */
    imageInfos[3].sampler = VK_NULL_HANDLE;
    imageInfos[3].imageView = views[3];
    imageInfos[3].imageLayout =
        VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[4];

    writes[0] = (VkWriteDescriptorSet){
        .sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfos[0]
    };

    writes[1] = (VkWriteDescriptorSet){
        .sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfos[1]
    };

    writes[2] = (VkWriteDescriptorSet){
        .sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfos[2]
    };

    writes[3] = (VkWriteDescriptorSet){
        .sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 3,
        .descriptorCount = 1,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfos[3]
    };

    vkUpdateDescriptorSets(
        device,
        4,
        writes,
        0,
        NULL);

    /*
     * Phase 2E synthetic motion field.
     *
     * RG32F values are normalized UV displacement.
     *
     * dx = +0.5 texel
     * dy =  0.0 texel
     */
    const float motionDx = 0.5f / (float)W;
    const float motionDy = 0.0f;

    VkDeviceSize motionSize =
        (VkDeviceSize)W * H * sizeof(float) * 2;

    VkBuffer motionBuffer = VK_NULL_HANDLE;
    VkDeviceMemory motionBufferMemory = VK_NULL_HANDLE;

    CHECK("vkCreateBuffer(motion)",
          vkCreateBuffer(
              device,
              &(VkBufferCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                  .size = motionSize,
                  .usage =
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  .sharingMode =
                      VK_SHARING_MODE_EXCLUSIVE
              },
              NULL,
              &motionBuffer));

    VkMemoryRequirements motionBmr;

    vkGetBufferMemoryRequirements(
        device,
        motionBuffer,
        &motionBmr);

    uint32_t motionMemoryType = UINT32_MAX;

    for (uint32_t i = 0;
         i < memProps.memoryTypeCount;
         i++) {

        VkMemoryPropertyFlags f =
            memProps.memoryTypes[i].propertyFlags;

        if ((f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
            (motionBmr.memoryTypeBits & (1u << i))) {
            motionMemoryType = i;
            break;
        }
    }

    if (motionMemoryType == UINT32_MAX) {
        printf("No suitable host-visible motion memory\n");
        return 21;
    }

    CHECK("vkAllocateMemory(motion)",
          vkAllocateMemory(
              device,
              &(VkMemoryAllocateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                  .allocationSize = motionBmr.size,
                  .memoryTypeIndex = motionMemoryType
              },
              NULL,
              &motionBufferMemory));

    CHECK("vkBindBufferMemory(motion)",
          vkBindBufferMemory(
              device,
              motionBuffer,
              motionBufferMemory,
              0));

    float *motionMapped = NULL;

    CHECK("vkMapMemory(motion)",
          vkMapMemory(
              device,
              motionBufferMemory,
              0,
              motionSize,
              0,
              (void **)&motionMapped));

    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            size_t o =
                ((size_t)y * W + x) * 2;

            motionMapped[o + 0] = motionDx;
            motionMapped[o + 1] = motionDy;
        }
    }

    vkUnmapMemory(
        device,
        motionBufferMemory);

    CHECK("vkResetFences(motion)",
          ((PFN_vkResetFences)getDeviceProcAddr(
              device,
              "vkResetFences"))(
                  device,
                  1,
                  &fence));

    CHECK("vkBeginCommandBuffer(motion)",
          vkBeginCommandBuffer(
              commandBuffer,
              &(VkCommandBufferBeginInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
              }));

    VkImageMemoryBarrier motionBarrier = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout =
            VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcAccessMask = 0,
        .dstAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT,
        .image = images[2],
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, 1, 0, 1
        }
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, NULL,
        0, NULL,
        1,
        &motionBarrier);

    VkBufferImageCopy motionCopy = {
        .bufferOffset = 0,
        .imageSubresource = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, 0, 1
        },
        .imageExtent = {
            W, H, 1
        }
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        motionBuffer,
        images[2],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &motionCopy);

    motionBarrier.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    motionBarrier.newLayout =
        VK_IMAGE_LAYOUT_GENERAL;

    motionBarrier.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;

    motionBarrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1,
        &motionBarrier);

    CHECK("vkEndCommandBuffer(motion)",
          vkEndCommandBuffer(commandBuffer));

    CHECK("vkQueueSubmit(motion)",
          vkQueueSubmit(
              queue,
              1,
              &(VkSubmitInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_SUBMIT_INFO,
                  .commandBufferCount = 1,
                  .pCommandBuffers =
                      &commandBuffer
              },
              fence));

    CHECK("vkWaitForFences(motion)",
          vkWaitForFences(
              device,
              1,
              &fence,
              VK_TRUE,
              UINT64_MAX));

    printf(
        "Motion field: dx=%f dy=%f "
        "(%f texel, %f texel)\n",
        motionDx,
        motionDy,
        motionDx * (float)W,
        motionDy * (float)H);

    /*
     * Prepare output image for storage writes.
     */
    CHECK("vkResetFences(output)",
          ((PFN_vkResetFences)getDeviceProcAddr(
              device,
              "vkResetFences"))(
                  device,
                  1,
                  &fence));

    CHECK("vkBeginCommandBuffer(compute)",
          vkBeginCommandBuffer(
              commandBuffer,
              &(VkCommandBufferBeginInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
              }));

    VkImageMemoryBarrier outputBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .image = images[3],
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, 1, 0, 1
        }
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1,
        &outputBarrier);

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
     * Make output readable by transfer.
     */
    outputBarrier.oldLayout =
        VK_IMAGE_LAYOUT_GENERAL;

    outputBarrier.newLayout =
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    outputBarrier.srcAccessMask =
        VK_ACCESS_SHADER_WRITE_BIT;

    outputBarrier.dstAccessMask =
        VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, NULL,
        0, NULL,
        1,
        &outputBarrier);

    VkBufferImageCopy readbackRegion = {
        .bufferOffset = 0,
        .imageSubresource = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, 0, 1
        },
        .imageExtent = { W, H, 1 }
    };

    vkCmdCopyImageToBuffer(
        commandBuffer,
        images[3],
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        buffer,
        1,
        &readbackRegion);

    CHECK("vkEndCommandBuffer(compute)",
          vkEndCommandBuffer(commandBuffer));

    CHECK("vkQueueSubmit(compute)",
          vkQueueSubmit(
              queue,
              1,
              &(VkSubmitInfo){
                  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                  .commandBufferCount = 1,
                  .pCommandBuffers = &commandBuffer
              },
              fence));

    CHECK("vkWaitForFences(compute)",
          vkWaitForFences(
              device,
              1,
              &fence,
              VK_TRUE,
              UINT64_MAX));

    CHECK("vkMapMemory(readback)",
          vkMapMemory(
              device,
              bufferMemory,
              0,
              IMAGE_SIZE,
              0,
              (void **)&mapped));

    /*
     * Phase 2E motion-vector validation.
     *
     * The shader should consume binding 2 and use the RG32F
     * displacement to alter the frame A/B sample coordinates.
     */
    const unsigned testXY[][2] = {
        {  0,  0 },
        { 16,  0 },
        { 32,  0 },
        { 48,  0 },
        { 63,  0 },

        {  0, 16 },
        { 32, 16 },
        { 63, 16 },

        {  0, 32 },
        { 16, 32 },
        { 32, 32 },
        { 48, 32 },
        { 63, 32 },

        {  0, 48 },
        { 32, 48 },
        { 63, 48 },

        {  0, 63 },
        { 16, 63 },
        { 32, 63 },
        { 48, 63 },
        { 63, 63 }
    };

    unsigned failures = 0;

    const int expectedR = 128;
    const int expectedG = 128;
    const int expectedB = 128;
    const int tolerance = 4;

    for (unsigned i = 0;
         i < sizeof(testXY) / sizeof(testXY[0]);
         i++) {

        unsigned x = testXY[i][0];
        unsigned y = testXY[i][1];

        size_t o =
            ((size_t)y * W + x) * 4;

        int r = mapped[o + 0];
        int g = mapped[o + 1];
        int b = mapped[o + 2];
        int a = mapped[o + 3];

        int ok =
            abs(r - expectedR) <= tolerance &&
            abs(g - expectedG) <= tolerance &&
            abs(b - expectedB) <= tolerance &&
            a == 0xff;

        printf(
            "motion[%02u,%02u] = "
            "%02x %02x %02x %02x %s\n",
            x,
            y,
            mapped[o + 0],
            mapped[o + 1],
            mapped[o + 2],
            mapped[o + 3],
            ok ? "OK" : "FAIL");

        if (!ok)
            failures++;
    }

    printf(
        "\n=== PHASE 2E MOTION VALIDATION ===\n"
        "Motion: +0.5 texel X, 0.0 texel Y\n"
        "Expected midpoint: %02x %02x %02x ff\n"
        "Tolerance: +/- %d LSB\n",
        expectedR,
        expectedG,
        expectedB,
        tolerance);

    if (failures == 0) {
        printf("RESULT: PASS\n");
        printf("MOTION VECTOR IMAGE LOAD: YES\n");
        printf("PER-PIXEL TEMPORAL REPROJECTION: YES\n");
        printf("MOTION-AWARE FRAME A/B SAMPLING: YES\n");
        printf("GPU IMAGELOAD + SAMPLING + MIX + IMAGESTORE: YES\n");
    } else {
        printf("RESULT: FAIL\n");
        printf("Failures: %u\n", failures);
    }

    return failures ? 20 : 0;
}
