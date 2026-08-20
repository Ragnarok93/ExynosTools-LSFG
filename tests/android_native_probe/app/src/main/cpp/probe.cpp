#include <jni.h>
#include <android/log.h>
#include <vulkan/vulkan.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

#define LOG_TAG "ExynosTools-Phase3C"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

struct ProbeResult {
    VkResult create_instance_result = VK_ERROR_INITIALIZATION_FAILED;
    uint32_t layer_count = 0;
    uint32_t device_count = 0;
    bool vortek_found = false;
    std::string device_summary;
};

std::string result_name(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

bool ensure_directory(const std::string& path) {
    if (mkdir(path.c_str(), 0700) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        struct stat st{};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    LOGE("mkdir(%s) failed: errno=%d (%s)", path.c_str(), errno, std::strerror(errno));
    return false;
}

bool write_layer_manifest(const std::string& directory, const std::string& native_library_dir) {
    const std::string manifest_path = directory + "/VkLayer_vortek_xclipse.json";
    std::ofstream manifest(manifest_path, std::ios::trunc);
    if (!manifest) {
        LOGE("Cannot write layer manifest %s: errno=%d (%s)",
             manifest_path.c_str(), errno, std::strerror(errno));
        return false;
    }

    manifest << "{\n"
             << "  \"file_format_version\": \"1.2.0\",\n"
             << "  \"layer\": {\n"
             << "    \"name\": \"VK_LAYER_VORTEK_XCLIPSE\",\n"
             << "    \"type\": \"GLOBAL\",\n"
             << "    \"library_path\": \"" << native_library_dir
             << "/libVkLayer_VortekXclipse.so\",\n"
             << "    \"api_version\": \"1.3.0\",\n"
             << "    \"implementation_version\": 1,\n"
             << "    \"description\": \"Vortek Xclipse in-process BCn compatibility wrapper\"\n"
             << "  }\n"
             << "}\n";

    if (!manifest) {
        LOGE("Failed while writing layer manifest %s", manifest_path.c_str());
        return false;
    }

    LOGI("Layer manifest: %s", manifest_path.c_str());
    LOGI("Layer library: %s/libVkLayer_VortekXclipse.so", native_library_dir.c_str());
    return true;
}

void enumerate_layers(ProbeResult& result) {
    uint32_t count = 0;
    VkResult vk_result = vkEnumerateInstanceLayerProperties(&count, nullptr);
    if (vk_result != VK_SUCCESS) {
        LOGE("vkEnumerateInstanceLayerProperties(count) failed: %s", result_name(vk_result).c_str());
        return;
    }

    std::vector<VkLayerProperties> properties(count);
    vk_result = vkEnumerateInstanceLayerProperties(&count, properties.data());
    if (vk_result != VK_SUCCESS && vk_result != VK_INCOMPLETE) {
        LOGE("vkEnumerateInstanceLayerProperties(data) failed: %s", result_name(vk_result).c_str());
        return;
    }

    result.layer_count = count;
    for (const auto& property : properties) {
        LOGI("Instance layer: %s api=%u.%u.%u",
             property.layerName,
             VK_VERSION_MAJOR(property.specVersion),
             VK_VERSION_MINOR(property.specVersion),
             VK_VERSION_PATCH(property.specVersion));
        if (std::strcmp(property.layerName, "VK_LAYER_VORTEK_XCLIPSE") == 0) {
            result.vortek_found = true;
        }
    }

    LOGI("Vortek enumeration: %s", result.vortek_found ? "FOUND" : "NOT FOUND");
}

ProbeResult run_instance_probe(bool enable_vortek) {
    ProbeResult result{};
    enumerate_layers(result);

    const char* enabled_layers[] = {"VK_LAYER_VORTEK_XCLIPSE"};

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = enable_vortek ? "ExynosTools Phase 3C Vortek" : "ExynosTools Phase 3C Baseline";
    app_info.applicationVersion = 1;
    app_info.pEngineName = "ExynosToolsAndroidNativeProbe";
    app_info.engineVersion = 1;
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    if (enable_vortek) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = enabled_layers;
    }

    VkInstance instance = VK_NULL_HANDLE;
    result.create_instance_result = vkCreateInstance(&create_info, nullptr, &instance);
    if (result.create_instance_result != VK_SUCCESS) {
        LOGE("%s vkCreateInstance failed: %s",
             enable_vortek ? "VORTEK" : "BASELINE",
             result_name(result.create_instance_result).c_str());
        return result;
    }

    uint32_t device_count = 0;
    VkResult vk_result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (vk_result != VK_SUCCESS) {
        LOGE("%s vkEnumeratePhysicalDevices(count) failed: %s",
             enable_vortek ? "VORTEK" : "BASELINE", result_name(vk_result).c_str());
        vkDestroyInstance(instance, nullptr);
        return result;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vk_result = vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    if (vk_result != VK_SUCCESS && vk_result != VK_INCOMPLETE) {
        LOGE("%s vkEnumeratePhysicalDevices(data) failed: %s",
             enable_vortek ? "VORTEK" : "BASELINE", result_name(vk_result).c_str());
        vkDestroyInstance(instance, nullptr);
        return result;
    }

    result.device_count = device_count;
    std::ostringstream summary;
    summary << "devices=" << device_count;

    for (uint32_t index = 0; index < device_count; ++index) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(devices[index], &properties);

        summary << " | GPU" << index
                << " name=\"" << properties.deviceName << "\""
                << " vendor=0x" << std::hex << properties.vendorID
                << " device=0x" << properties.deviceID << std::dec
                << " api=" << VK_VERSION_MAJOR(properties.apiVersion)
                << "." << VK_VERSION_MINOR(properties.apiVersion)
                << "." << VK_VERSION_PATCH(properties.apiVersion)
                << " driverVersion=0x" << std::hex << properties.driverVersion << std::dec;

        LOGI("%s GPU%u: name=%s vendor=0x%04x device=0x%04x api=%u.%u.%u driverVersion=0x%08x",
             enable_vortek ? "VORTEK" : "BASELINE",
             index,
             properties.deviceName,
             properties.vendorID,
             properties.deviceID,
             VK_VERSION_MAJOR(properties.apiVersion),
             VK_VERSION_MINOR(properties.apiVersion),
             VK_VERSION_PATCH(properties.apiVersion),
             properties.driverVersion);
    }

    result.device_summary = summary.str();
    vkDestroyInstance(instance, nullptr);
    return result;
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_exynostools_androidprobe_MainActivity_runProbe(
    JNIEnv* env,
    jclass,
    jstring native_library_dir_java,
    jstring files_dir_java) {
    const char* native_library_dir_chars = env->GetStringUTFChars(native_library_dir_java, nullptr);
    const char* files_dir_chars = env->GetStringUTFChars(files_dir_java, nullptr);
    const std::string native_library_dir = native_library_dir_chars ? native_library_dir_chars : "";
    const std::string files_dir = files_dir_chars ? files_dir_chars : "";
    env->ReleaseStringUTFChars(native_library_dir_java, native_library_dir_chars);
    env->ReleaseStringUTFChars(files_dir_java, files_dir_chars);

    LOGI("=== PHASE 3C ANDROID-NATIVE VENDOR ICD PROBE ===");
    LOGI("nativeLibraryDir=%s", native_library_dir.c_str());
    LOGI("filesDir=%s", files_dir.c_str());

    if (native_library_dir.empty() || files_dir.empty()) {
        return env->NewStringUTF("Phase 3C ERROR: invalid Android paths");
    }

    const std::string layer_dir = files_dir + "/vulkan-layer";
    if (!ensure_directory(layer_dir)) {
        return env->NewStringUTF("Phase 3C ERROR: cannot create layer directory");
    }
    if (!write_layer_manifest(layer_dir, native_library_dir)) {
        return env->NewStringUTF("Phase 3C ERROR: cannot write layer manifest");
    }

    setenv("VK_LAYER_PATH", layer_dir.c_str(), 1);
    setenv("VK_LOADER_DEBUG", "error,warn,layer,driver", 1);
    unsetenv("VK_INSTANCE_LAYERS");
    unsetenv("VK_DRIVER_FILES");
    unsetenv("VK_ICD_FILENAMES");

    LOGI("VK_LAYER_PATH=%s", layer_dir.c_str());
    LOGI("Running baseline without Vortek...");
    ProbeResult baseline = run_instance_probe(false);

    LOGI("Running Android-native vendor ICD test with Vortek enabled...");
    ProbeResult vortek = run_instance_probe(true);

    std::ostringstream output;
    output << "Phase 3C Android-native probe\n"
           << "=============================\n"
           << "Baseline vkCreateInstance: " << result_name(baseline.create_instance_result) << "\n"
           << "Baseline devices: " << baseline.device_count << "\n"
           << baseline.device_summary << "\n\n"
           << "Vortek layer enumeration: " << (vortek.vortek_found ? "FOUND" : "NOT FOUND") << "\n"
           << "Vortek vkCreateInstance: " << result_name(vortek.create_instance_result) << "\n"
           << "Vortek devices: " << vortek.device_count << "\n"
           << vortek.device_summary << "\n";

    if (baseline.create_instance_result == VK_SUCCESS &&
        vortek.vortek_found &&
        vortek.create_instance_result == VK_SUCCESS &&
        vortek.device_count > 0) {
        LOGI("PHASE 3C RESULT: SUCCESS - Android process reached a Vulkan device with Vortek enabled");
    } else {
        LOGW("PHASE 3C RESULT: NOT COMPLETE - inspect logcat for vendor ICD/linker details");
    }

    return env->NewStringUTF(output.str().c_str());
}
