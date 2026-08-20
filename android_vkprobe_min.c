#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef int32_t VkResult;
typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkInstance;

#define VK_SUCCESS 0
#define VK_API_VERSION_1_0 ((1u << 22))

typedef struct {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char * const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char * const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef VkResult (*PFN_vkEnumerateInstanceVersion)(uint32_t *);
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(
    const char *, uint32_t *, void *);
typedef VkResult (*PFN_vkCreateInstance)(
    const VkInstanceCreateInfo *, const void *, VkInstance *);
typedef void (*PFN_vkDestroyInstance)(VkInstance, const void *);

int main(void)
{
    printf("=== MINIMAL ANDROID VULKAN INSTANCE TEST ===\n");
    fflush(stdout);

    void *lib = dlopen("/system/lib64/libvulkan.so",
                       RTLD_NOW | RTLD_LOCAL);

    if (!lib) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }

    printf("loader: SUCCESS\n");
    fflush(stdout);

    PFN_vkEnumerateInstanceVersion enumerateVersion =
        (PFN_vkEnumerateInstanceVersion)
        dlsym(lib, "vkEnumerateInstanceVersion");

    PFN_vkEnumerateInstanceExtensionProperties enumerateExtensions =
        (PFN_vkEnumerateInstanceExtensionProperties)
        dlsym(lib, "vkEnumerateInstanceExtensionProperties");

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance)
        dlsym(lib, "vkCreateInstance");

    PFN_vkDestroyInstance destroyInstance =
        (PFN_vkDestroyInstance)
        dlsym(lib, "vkDestroyInstance");

    printf("vkEnumerateInstanceVersion: %p\n", (void *)enumerateVersion);
    printf("vkEnumerateInstanceExtensionProperties: %p\n",
           (void *)enumerateExtensions);
    printf("vkCreateInstance: %p\n", (void *)createInstance);
    printf("vkDestroyInstance: %p\n", (void *)destroyInstance);
    fflush(stdout);

    uint32_t api = 0;
    VkResult r = enumerateVersion(&api);

    printf("API result: %d\n", r);
    printf("API: %u.%u.%u\n",
           (api >> 22) & 0x7f,
           (api >> 12) & 0x3ff,
           api & 0xfff);
    fflush(stdout);

    uint32_t count = 0;

    r = enumerateExtensions(NULL, &count, NULL);

    printf("Extension query result: %d\n", r);
    printf("Extension count: %u\n", count);
    fflush(stdout);

    /*
     * Deliberately minimal instance:
     * - no pNext
     * - no flags
     * - no layers
     * - no extensions
     */
    VkApplicationInfo app = {
        .sType = 0,
        .pNext = NULL,
        .pApplicationName = "LSFGProbe",
        .applicationVersion = 1,
        .pEngineName = "LSFGProbe",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_0
    };

    VkInstanceCreateInfo ci = {
        .sType = 1,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &app,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL
    };

    VkInstance instance = 0;

    printf("\n=== CALLING vkCreateInstance ===\n");
    fflush(stdout);

    r = createInstance(&ci, NULL, &instance);

    printf("vkCreateInstance returned: %d\n", r);
    printf("instance: 0x%llx\n",
           (unsigned long long)instance);
    fflush(stdout);

    if (r == VK_SUCCESS && instance) {
        printf("INSTANCE CREATION SUCCESS\n");
        fflush(stdout);

        destroyInstance(instance, NULL);

        printf("INSTANCE DESTROY SUCCESS\n");
        fflush(stdout);
    } else {
        printf("INSTANCE CREATION FAILED\n");
        fflush(stdout);
    }

    dlclose(lib);
    return 0;
}
