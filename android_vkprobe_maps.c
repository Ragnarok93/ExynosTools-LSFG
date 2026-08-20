#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <vulkan/vulkan.h>

int main(void)
{
    printf("=== BEFORE LOADER ===\n");
    fflush(stdout);

    void *lib = dlopen("/system/lib64/libvulkan.so",
                       RTLD_NOW | RTLD_LOCAL);

    if (!lib) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }

    printf("loader loaded\n");
    fflush(stdout);

    uint32_t api = 0;
    VkResult r = vkEnumerateInstanceVersion(&api);

    printf("vkEnumerateInstanceVersion = %d (%u.%u.%u)\n",
           r,
           VK_VERSION_MAJOR(api),
           VK_VERSION_MINOR(api),
           VK_VERSION_PATCH(api));
    fflush(stdout);

    printf("\n=== /proc/self/maps BEFORE vkCreateInstance ===\n");
    fflush(stdout);

    FILE *f = fopen("/proc/self/maps", "r");
    if (f) {
        char line[512];

        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "vulkan") ||
                strstr(line, "sgpu") ||
                strstr(line, "samsung") ||
                strstr(line, "gralloc") ||
                strstr(line, "graphicbuffer"))
                printf("%s", line);
        }

        fclose(f);
    }

    printf("\n=== CALLING vkCreateInstance ===\n");
    fflush(stdout);

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "LSFGProbe",
        .applicationVersion = 1,
        .pEngineName = "LSFGProbe",
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

    r = vkCreateInstance(&ci, NULL, &instance);

    printf("vkCreateInstance returned %d\n", r);
    printf("instance = %p\n", (void *)instance);
    fflush(stdout);

    return 0;
}
