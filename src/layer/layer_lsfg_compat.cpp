#include "layer_lsfg_compat.h"

#include <cstdlib>
#include <string>

namespace {

bool non_empty_environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
}

bool lsfg_process_environment_present() {
    // GameNative sets LSFG_PROCESS for the target application and LSFG_CONFIG
    // for the configuration consumed by lsfg-vk.  Requiring both avoids
    // accidentally entering LSFG mode in an unrelated process that happens
    // to inherit one variable.
    return non_empty_environment_value("LSFG_PROCESS") &&
           non_empty_environment_value("LSFG_CONFIG");
}

}  // namespace

bool exynos_lsfg_process_active() {
    return lsfg_process_environment_present();
}

LayerLsfgCompatSnapshot snapshot_lsfg_compat() {
    LayerLsfgCompatSnapshot snapshot;
    snapshot.process_environment_present = lsfg_process_environment_present();
    snapshot.enabled = snapshot.process_environment_present;
    return snapshot;
}

