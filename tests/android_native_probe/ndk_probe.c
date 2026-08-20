#include <android/log.h>
#include <vulkan/vulkan.h>
#include <dlfcn.h>

int main(void) {
    __android_log_print(ANDROID_LOG_INFO, "ndk_probe", "hello");

    void *p = dlopen("libvulkan.so", RTLD_NOW);
    if (p) {
        dlclose(p);
    }

    return VK_SUCCESS;
}
