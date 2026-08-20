#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

int main(void) {
    uint32_t api = VK_API_VERSION_1_0;

    VkResult r = vkEnumerateInstanceVersion(&api);

    printf("vkEnumerateInstanceVersion: %d\n", r);
    printf("Vulkan API: %u.%u.%u\n",
           VK_VERSION_MAJOR(api),
           VK_VERSION_MINOR(api),
           VK_VERSION_PATCH(api));

    uint32_t count = 0;

    r = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);

    printf("vkEnumerateInstanceExtensionProperties: %d\n", r);
    printf("Instance extensions: %u\n", count);

    if (r != VK_SUCCESS)
        return 1;

    VkExtensionProperties *ext =
        calloc(count, sizeof(*ext));

    if (!ext)
        return 1;

    r = vkEnumerateInstanceExtensionProperties(NULL, &count, ext);

    printf("Extension enumeration: %d\n", r);

    for (uint32_t i = 0; i < count; i++)
        printf("  %s\n", ext[i].extensionName);

    free(ext);
    return 0;
}
