#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef int32_t VkResult;
typedef uint64_t VkInstance;
typedef uint64_t VkPhysicalDevice;

typedef struct {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    const void *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char * const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char * const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct {
    uint32_t sType;
    const void *pNext;
    uint32_t apiVersion;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
} VkApplicationInfo;

typedef struct {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    uint32_t limits[256];
    uint8_t rest[4096];
} VkPhysicalDeviceProperties;

typedef VkResult (*PFN_vkEnumerateInstanceVersion)(uint32_t *);
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(
    const char *, uint32_t *, void *);
typedef VkResult (*PFN_vkCreateInstance)(
    const VkInstanceCreateInfo *, const void *, VkInstance *);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(
    VkInstance, uint32_t *, VkPhysicalDevice *);
typedef void (*PFN_vkGetPhysicalDeviceProperties)(
    VkPhysicalDevice, VkPhysicalDeviceProperties *);
typedef void (*PFN_vkDestroyInstance)(
    VkInstance, const void *);

int main(void) {
    void *lib = dlopen(
        "/system/lib64/libvulkan.so",
        RTLD_NOW | RTLD_LOCAL
    );

    if (!lib) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }

    printf("=== ANDROID VULKAN LOADER ===\n");
    printf("dlopen: SUCCESS\n");

    PFN_vkEnumerateInstanceVersion enumerateVersion =
        (PFN_vkEnumerateInstanceVersion)
        dlsym(lib, "vkEnumerateInstanceVersion");

    PFN_vkEnumerateInstanceExtensionProperties enumerateExtensions =
        (PFN_vkEnumerateInstanceExtensionProperties)
        dlsym(lib, "vkEnumerateInstanceExtensionProperties");

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance)
        dlsym(lib, "vkCreateInstance");

    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)
        dlsym(lib, "vkEnumeratePhysicalDevices");

    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties =
        (PFN_vkGetPhysicalDeviceProperties)
        dlsym(lib, "vkGetPhysicalDeviceProperties");

    PFN_vkDestroyInstance destroyInstance =
        (PFN_vkDestroyInstance)
        dlsym(lib, "vkDestroyInstance");

    printf("vkEnumerateInstanceVersion: %p\n",
           (void *)enumerateVersion);
    printf("vkEnumerateInstanceExtensionProperties: %p\n",
           (void *)enumerateExtensions);
    printf("vkCreateInstance: %p\n",
           (void *)createInstance);
    printf("vkEnumeratePhysicalDevices: %p\n",
           (void *)enumeratePhysicalDevices);
    printf("vkGetPhysicalDeviceProperties: %p\n",
           (void *)getPhysicalDeviceProperties);
    printf("vkDestroyInstance: %p\n",
           (void *)destroyInstance);

    uint32_t api = 0;

    VkResult r = enumerateVersion(&api);

    printf("\n=== API VERSION ===\n");
    printf("Result: %d\n", r);
    printf("API: %u.%u.%u\n",
           (api >> 22) & 0x7f,
           (api >> 12) & 0x3ff,
           api & 0xfff);

    uint32_t extCount = 0;

    r = enumerateExtensions(NULL, &extCount, NULL);

    printf("\n=== INSTANCE EXTENSIONS ===\n");
    printf("Result: %d\n", r);
    printf("Count: %u\n", extCount);

    if (r != 0)
        return 2;

    void *extensions = calloc(extCount, 256);

    if (!extensions)
        return 3;

    r = enumerateExtensions(NULL, &extCount, extensions);

    printf("Enumeration result: %d\n", r);

    free(extensions);

    printf("\n=== CREATE INSTANCE ===\n");

    VkApplicationInfo app = {
        .sType = 0,
        .pNext = NULL,
        .apiVersion = api,
        .pApplicationName = "ExynosTools-LSFG",
        .applicationVersion = 1,
        .pEngineName = "LSFG Probe",
        .engineVersion = 1
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

    r = createInstance(&ci, NULL, &instance);

    printf("vkCreateInstance -> %d\n", r);
    printf("VkInstance: 0x%016llx\n",
           (unsigned long long)instance);

    if (r != 0 || !instance) {
        printf("\nDRIVER INITIALIZATION FAILED\n");
        dlclose(lib);
        return 4;
    }

    printf("\n=== PHYSICAL DEVICES ===\n");

    uint32_t deviceCount = 0;

    r = enumeratePhysicalDevices(
        instance,
        &deviceCount,
        NULL
    );

    printf("First enumeration -> %d\n", r);
    printf("Device count: %u\n", deviceCount);

    if (r == 0 && deviceCount > 0) {
        VkPhysicalDevice *devices =
            calloc(deviceCount, sizeof(VkPhysicalDevice));

        if (!devices)
            return 5;

        r = enumeratePhysicalDevices(
            instance,
            &deviceCount,
            devices
        );

        printf("Second enumeration -> %d\n", r);

        for (uint32_t i = 0; i < deviceCount; i++) {
            VkPhysicalDeviceProperties props;
            memset(&props, 0, sizeof(props));

            getPhysicalDeviceProperties(
                devices[i],
                &props
            );

            printf(
                "GPU %u: %s | vendor=0x%04x device=0x%04x type=%u API=%u.%u.%u\n",
                i,
                props.deviceName,
                props.vendorID,
                props.deviceID,
                props.deviceType,
                (props.apiVersion >> 22) & 0x7f,
                (props.apiVersion >> 12) & 0x3ff,
                props.apiVersion & 0xfff
            );
        }

        free(devices);
    }

    destroyInstance(instance, NULL);

    printf("\n=== RESULT ===\n");
    printf("Android Vulkan loader successfully initialized the driver.\n");

    dlclose(lib);
    return 0;
}
