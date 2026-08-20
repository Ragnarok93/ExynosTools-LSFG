#include "layer_lsfg_compat.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

bool non_empty_environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
}

bool environment_value_is_true(const char* name) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return false;
    }
    return std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 ||
           std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

bool lsfg_process_environment_present() {
    // GameNative's LSFG integration marks the target process with both
    // variables. Requiring the pair avoids false positives from unrelated
    // inherited configuration.
    return non_empty_environment_value("LSFG_PROCESS") &&
           non_empty_environment_value("LSFG_CONFIG");
}

bool lsfg_explicitly_disabled() {
    // Permit the host to suppress LSFG compatibility without changing the
    // GameNative markers. This is useful for diagnostics and keeps the
    // compatibility decision process-local.
    return environment_value_is_true("EXYNOS_LSFG_DISABLE");
}

}  // namespace

bool exynos_lsfg_process_active() {
    return lsfg_process_environment_present() && !lsfg_explicitly_disabled();
}

LayerLsfgCompatSnapshot snapshot_lsfg_compat() {
    LayerLsfgCompatSnapshot snapshot;
    snapshot.process_environment_present = lsfg_process_environment_present();
    snapshot.enabled = snapshot.process_environment_present && !lsfg_explicitly_disabled();
    return snapshot;
}

