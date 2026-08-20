#include <stdio.h>
#include <dlfcn.h>

int main(void)
{
    void *lib = dlopen("/system/lib64/libvulkan.so",
                       RTLD_NOW | RTLD_LOCAL);

    if (!lib) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }

    const char *symbols[] = {
        "vkGetAndroidHardwareBufferPropertiesANDROID",
        "vkGetMemoryAndroidHardwareBufferANDROID"
    };

    for (unsigned i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        void *p = dlsym(lib, symbols[i]);

        printf("%-55s %s\n",
               symbols[i],
               p ? "PRESENT" : "MISSING");
    }

    return 0;
}
