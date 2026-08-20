#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <vulkan/vulkan.h>

static uint32_t find_memory_type(
    const VkPhysicalDeviceMemoryProperties *mem,
    uint32_t type_bits,
    VkMemoryPropertyFlags wanted)
{
    for (uint32_t i = 0; i < mem->memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (mem->memoryTypes[i].propertyFlags & wanted) == wanted) {
            return i;
        }
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
    if (n <= 0 || (n % 4) != 0) {
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
        *data = NULL;
        fclose(f);
        return -5;
    }

    fclose(f);

    *size = (size_t)n;
    return 0;
}

#define LOAD_INSTANCE(name)                                      \
    PFN_##name name =                                           \
        (PFN_##name)getProc(instance, #name);                   \
    if (!name) {                                                 \
        printf("MISSING INSTANCE FUNCTION: %s\n", #name);       \
        return 10;                                               \
    }

#define LOAD_DEVICE(name)                                       \
    PFN_##name name =                                           \
        (PFN_##name)vkGetDeviceProcAddr(device, #name);         \
    if (!name) {                                                 \
        printf("MISSING DEVICE FUNCTION: %s\n", #name);         \
        return 100;                                              \
    }

int main(void)
{
    printf("=== XCLIPSE 940 REAL COMPUTE EXECUTION PROBE ===\n");
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
        printf("Vulkan loader entry points missing\n");
        return 2;
    }

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LSFG-Compute-Probe",
        .applicationVersion = 1,
        .pEngineName = "LSFG-Compute-Probe",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_0
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

    LOAD_INSTANCE(vkEnumeratePhysicalDevices);
    LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
    LOAD_INSTANCE(vkCreateDevice);
    LOAD_INSTANCE(vkGetDeviceProcAddr);

    uint32_t physicalCount = 0;

    r = vkEnumeratePhysicalDevices(
        instance,
        &physicalCount,
        NULL);

    printf("Physical device count: %u\n", physicalCount);

    if (r != VK_SUCCESS || physicalCount == 0) {
        printf("No physical devices: %d\n", r);
        return 4;
    }

    VkPhysicalDevice physicalDevices[8];

    if (physicalCount > 8)
        physicalCount = 8;

    r = vkEnumeratePhysicalDevices(
        instance,
        &physicalCount,
        physicalDevices);

    if (r != VK_SUCCESS)
        return 5;

    VkPhysicalDevice physical = physicalDevices[0];

    VkPhysicalDeviceProperties properties;

    vkGetPhysicalDeviceProperties(
        physical,
        &properties);

    printf("GPU: %s\n", properties.deviceName);

    printf(
        "Vulkan API: %u.%u.%u\n",
        VK_VERSION_MAJOR(properties.apiVersion),
        VK_VERSION_MINOR(properties.apiVersion),
        VK_VERSION_PATCH(properties.apiVersion));

    /*
     * Find compute queue.
     *
     * Prefer a compute-only queue. If none exists,
     * fall back to graphics + compute.
     */
    uint32_t queueFamilyCount = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(
        physical,
        &queueFamilyCount,
        NULL);

    printf("Queue family count: %u\n", queueFamilyCount);

    VkQueueFamilyProperties queueFamilies[16];

    if (queueFamilyCount > 16)
        queueFamilyCount = 16;

    vkGetPhysicalDeviceQueueFamilyProperties(
        physical,
        &queueFamilyCount,
        queueFamilies);

    uint32_t computeFamily = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        VkQueueFlags flags = queueFamilies[i].queueFlags;

        printf(
            "Queue family %u: flags=0x%08x queues=%u\n",
            i,
            flags,
            queueFamilies[i].queueCount);

        if ((flags & VK_QUEUE_COMPUTE_BIT) &&
            !(flags & VK_QUEUE_GRAPHICS_BIT)) {
            computeFamily = i;
            break;
        }
    }

    if (computeFamily == UINT32_MAX) {
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            VkQueueFlags flags = queueFamilies[i].queueFlags;

            if ((flags & VK_QUEUE_GRAPHICS_BIT) &&
                (flags & VK_QUEUE_COMPUTE_BIT)) {
                computeFamily = i;
                break;
            }
        }
    }

    if (computeFamily == UINT32_MAX) {
        printf("No compute queue available\n");
        return 6;
    }

    printf(
        "Selected compute queue family: %u\n",
        computeFamily);

    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = computeFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    VkDeviceCreateInfo deviceCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCI
    };

    VkDevice device = VK_NULL_HANDLE;

    r = vkCreateDevice(
        physical,
        &deviceCI,
        NULL,
        &device);

    printf("vkCreateDevice: %d\n", r);

    if (r != VK_SUCCESS)
        return 7;

    /*
     * Device functions.
     */
    LOAD_DEVICE(vkGetDeviceQueue);
    LOAD_DEVICE(vkCreateBuffer);
    LOAD_DEVICE(vkGetBufferMemoryRequirements);
    LOAD_DEVICE(vkAllocateMemory);
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
    LOAD_DEVICE(vkCmdBindPipeline);
    LOAD_DEVICE(vkCmdBindDescriptorSets);
    LOAD_DEVICE(vkCmdDispatch);
    LOAD_DEVICE(vkCmdPipelineBarrier);
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
        return 8;
    }

    printf("Compute queue acquired: YES\n");

    /*
     * 64 uint32 values = 256 bytes.
     */
    const VkDeviceSize bufferSize =
        64 * sizeof(uint32_t);

    VkBufferCreateInfo bufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkBuffer buffer = VK_NULL_HANDLE;

    r = vkCreateBuffer(
        device,
        &bufferCI,
        NULL,
        &buffer);

    printf("vkCreateBuffer: %d\n", r);

    if (r != VK_SUCCESS)
        return 9;

    VkMemoryRequirements memoryRequirements;

    vkGetBufferMemoryRequirements(
        device,
        buffer,
        &memoryRequirements);

    printf(
        "Buffer allocation size: %llu\n",
        (unsigned long long)memoryRequirements.size);

    VkPhysicalDeviceMemoryProperties memoryProperties;

    vkGetPhysicalDeviceMemoryProperties(
        physical,
        &memoryProperties);

    uint32_t memoryType = find_memory_type(
        &memoryProperties,
        memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (memoryType == UINT32_MAX) {
        printf("No host-visible coherent memory type\n");
        return 10;
    }

    printf("Buffer memory type: %u\n", memoryType);

    VkMemoryAllocateInfo memoryAI = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = memoryType
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;

    r = vkAllocateMemory(
        device,
        &memoryAI,
        NULL,
        &memory);

    printf("vkAllocateMemory: %d\n", r);

    if (r != VK_SUCCESS)
        return 11;

    r = vkBindBufferMemory(
        device,
        buffer,
        memory,
        0);

    printf("vkBindBufferMemory: %d\n", r);

    if (r != VK_SUCCESS)
        return 12;

    uint32_t *mapped = NULL;

    r = vkMapMemory(
        device,
        memory,
        0,
        bufferSize,
        0,
        (void **)&mapped);

    printf("vkMapMemory: %d\n", r);

    if (r != VK_SUCCESS)
        return 13;

    /*
     * Fill with a sentinel so we can distinguish
     * "shader never executed" from shader output.
     */
    for (uint32_t i = 0; i < 64; ++i)
        mapped[i] = 0xDEADBEEFu;

    vkUnmapMemory(device, memory);

    /*
     * Load validated SPIR-V.
     */
    uint32_t *spirv = NULL;
    size_t spirvSize = 0;

    int loadResult = load_spirv(
        "/data/data/com.termux/files/home/ExynosTools-LSFG/lsfg_compute_probe.spv",
        &spirv,
        &spirvSize);

    if (loadResult != 0) {
        printf(
            "Failed to load SPIR-V: %d\n",
            loadResult);
        return 14;
    }

    printf(
        "SPIR-V size: %zu bytes\n",
        spirvSize);

    VkShaderModuleCreateInfo shaderCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirvSize,
        .pCode = spirv
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;

    r = vkCreateShaderModule(
        device,
        &shaderCI,
        NULL,
        &shaderModule);

    free(spirv);

    printf("vkCreateShaderModule: %d\n", r);

    if (r != VK_SUCCESS)
        return 15;

    /*
     * Descriptor set:
     * binding 0 = storage buffer.
     */
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };

    VkDescriptorSetLayoutCreateInfo setLayoutCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;

    r = vkCreateDescriptorSetLayout(
        device,
        &setLayoutCI,
        NULL,
        &setLayout);

    printf(
        "vkCreateDescriptorSetLayout: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 16;

    VkPipelineLayoutCreateInfo pipelineLayoutCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout
    };

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    r = vkCreatePipelineLayout(
        device,
        &pipelineLayoutCI,
        NULL,
        &pipelineLayout);

    printf(
        "vkCreatePipelineLayout: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 17;

    VkPipelineShaderStageCreateInfo shaderStage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shaderModule,
        .pName = "main"
    };

    VkComputePipelineCreateInfo pipelineCI = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
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

    printf(
        "vkCreateComputePipelines: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 18;

    /*
     * Descriptor pool and descriptor set.
     */
    VkDescriptorPoolSize poolSize = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1
    };

    VkDescriptorPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    r = vkCreateDescriptorPool(
        device,
        &poolCI,
        NULL,
        &descriptorPool);

    printf(
        "vkCreateDescriptorPool: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 19;

    VkDescriptorSetAllocateInfo setAI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &setLayout
    };

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    r = vkAllocateDescriptorSets(
        device,
        &setAI,
        &descriptorSet);

    printf(
        "vkAllocateDescriptorSets: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 20;

    VkDescriptorBufferInfo bufferInfo = {
        .buffer = buffer,
        .offset = 0,
        .range = bufferSize
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &bufferInfo
    };

    vkUpdateDescriptorSets(
        device,
        1,
        &write,
        0,
        NULL);

    /*
     * Command pool.
     */
    VkCommandPoolCreateInfo commandPoolCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = computeFamily
    };

    VkCommandPool commandPool = VK_NULL_HANDLE;

    r = vkCreateCommandPool(
        device,
        &commandPoolCI,
        NULL,
        &commandPool);

    printf(
        "vkCreateCommandPool: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 21;

    VkCommandBufferAllocateInfo commandBufferAI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    r = vkAllocateCommandBuffers(
        device,
        &commandBufferAI,
        &commandBuffer);

    printf(
        "vkAllocateCommandBuffers: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 22;

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };

    r = vkBeginCommandBuffer(
        commandBuffer,
        &beginInfo);

    printf(
        "vkBeginCommandBuffer: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 23;

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

    /*
     * Shader local size is 64x1x1.
     * One workgroup therefore executes 64 invocations.
     */
    vkCmdDispatch(
        commandBuffer,
        1,
        1,
        1);

    /*
     * Make shader writes visible to subsequent host reads.
     */
    VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = 0,
        .size = bufferSize
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        NULL,
        1,
        &barrier,
        0,
        NULL);

    r = vkEndCommandBuffer(commandBuffer);

    printf(
        "vkEndCommandBuffer: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 24;

    /*
     * Submit.
     */
    VkFenceCreateInfo fenceCI = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };

    VkFence fence = VK_NULL_HANDLE;

    r = vkCreateFence(
        device,
        &fenceCI,
        NULL,
        &fence);

    printf(
        "vkCreateFence: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 25;

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

    printf(
        "vkQueueSubmit: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 26;

    r = vkWaitForFences(
        device,
        1,
        &fence,
        VK_TRUE,
        UINT64_MAX);

    printf(
        "vkWaitForFences: %d\n",
        r);

    if (r != VK_SUCCESS)
        return 27;

    /*
     * Read back the 64 shader-generated values.
     */
    r = vkMapMemory(
        device,
        memory,
        0,
        bufferSize,
        0,
        (void **)&mapped);

    printf(
        "vkMapMemory(readback): %d\n",
        r);

    if (r != VK_SUCCESS)
        return 28;

    uint32_t failures = 0;

    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t expected = 0x4C534647u + i;

        printf(
            "value[%02u] = 0x%08x expected=0x%08x %s\n",
            i,
            mapped[i],
            expected,
            mapped[i] == expected ? "OK" : "FAIL");

        if (mapped[i] != expected)
            ++failures;
    }

    vkUnmapMemory(device, memory);

    printf("\n=== COMPUTE EXECUTION RESULT ===\n");

    if (failures == 0) {
        printf("RESULT: PASS\n");
        printf("64/64 shader writes verified.\n");
        printf("REAL VULKAN COMPUTE EXECUTION: YES\n");
    } else {
        printf("RESULT: FAIL\n");
        printf("%u/64 shader writes incorrect.\n", failures);
        printf("REAL VULKAN COMPUTE EXECUTION: NOT VERIFIED\n");
    }

    /*
     * Cleanup.
     */
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyShaderModule(device, shaderModule, NULL);
    vkDestroyDescriptorSetLayout(device, setLayout, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);

    /*
     * vkDestroyInstance is intentionally fetched directly because
     * it is an instance-level function.
     */
    PFN_vkDestroyInstance destroyInstance =
        (PFN_vkDestroyInstance)getProc(
            instance,
            "vkDestroyInstance");

    if (destroyInstance)
        destroyInstance(instance, NULL);

    dlclose(lib);

    return failures ? 29 : 0;
}
