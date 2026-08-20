#include "layer_lsfg_compat.h"

#include <cstdlib>
#include <string>

namespace {

bool non_empty_environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
}

bool lsfg_process_environment_present() {
    // GameNative's LSFG integration marks the target process with both
    // variables. Requiring the pair avoids false positives from unrelated
    // inherited configuration.
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

