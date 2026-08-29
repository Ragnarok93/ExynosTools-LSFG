#include "layer_lsfg_compat.h"

#include <cstdlib>
#include <string>

namespace {

constexpr const char* kLsfgLayerName = "VK_LAYER_LS_frame_generation";

bool non_empty_environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
}

bool environment_list_contains(const char* name, const char* expected) {
    const char* raw = std::getenv(name);
    if (!raw || raw[0] == '\0') return false;

    std::string token;
    for (const char* p = raw;; ++p) {
        const char ch = *p;
        const bool separator = ch == ':' || ch == ';' || ch == ',' || ch == '\0';
        if (!separator) {
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') token.push_back(ch);
        } else {
            if (token == expected) return true;
            token.clear();
            if (ch == '\0') break;
        }
    }
    return false;
}

bool lsfg_layer_activation_present() {
    return environment_list_contains("VK_INSTANCE_LAYERS", kLsfgLayerName) ||
           environment_list_contains("VK_LOADER_LAYERS_ENABLE", kLsfgLayerName);
}

bool lsfg_launch_environment_present() {
    // Current GameNative targeted mode intentionally does not export
    // LSFG_PROCESS: that variable overrides process identity inside lsfg-vk and
    // would be inherited by Wine/Zink helpers.  Explicit Vulkan-layer selection
    // is the authoritative marker for modern launches.  Keep LSFG_PROCESS as a
    // legacy fallback so older GameNative builds remain compatible.
    if (!non_empty_environment_value("LSFG_CONFIG")) return false;
    return lsfg_layer_activation_present() ||
           non_empty_environment_value("LSFG_PROCESS");
}

}  // namespace

bool exynos_lsfg_process_active() {
    return lsfg_launch_environment_present();
}

LayerLsfgCompatSnapshot snapshot_lsfg_compat() {
    LayerLsfgCompatSnapshot snapshot;
    snapshot.process_environment_present = lsfg_launch_environment_present();
    snapshot.enabled = snapshot.process_environment_present;
    return snapshot;
}
