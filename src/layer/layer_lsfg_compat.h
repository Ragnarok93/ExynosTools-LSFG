#pragma once

// LSFG compatibility state is intentionally separate from BCn emulation.
//
// Current GameNative targeted activation supplies LSFG_CONFIG and explicitly
// enables VK_LAYER_LS_frame_generation through the Vulkan loader. It
// intentionally does not export LSFG_PROCESS because that overrides process
// identity inside lsfg-vk and is inherited by Wine/Zink helper processes.
// Older GameNative builds may still provide LSFG_PROCESS, so the compatibility
// detector accepts that marker as a fallback when LSFG_CONFIG is also present.

struct LayerLsfgCompatSnapshot {
    bool enabled = false;
    // Retained for source compatibility. This now means that a valid LSFG
    // launch environment was detected, not specifically LSFG_PROCESS.
    bool process_environment_present = false;
};

// Returns true when LSFG_CONFIG is present and either the LSFG Vulkan layer is
// explicitly selected (current targeted mode) or LSFG_PROCESS is present
// (legacy mode). Executable/vendor names are never used for detection.
bool exynos_lsfg_process_active();

// Returns a point-in-time snapshot of the LSFG compatibility state.
LayerLsfgCompatSnapshot snapshot_lsfg_compat();
