#pragma once

// LSFG compatibility state is intentionally separate from BCn emulation.
//
// GameNative's LSFG Vulkan layer sets LSFG_PROCESS/LSFG_CONFIG in the
// process environment.  This module only detects that context for now.
// Later patches will use the state to make ExynosTools transparent to
// LSFG's feature negotiation, external-memory/AHardwareBuffer handling,
// and synchronization paths.

struct LayerLsfgCompatSnapshot {
    bool enabled = false;
    bool process_environment_present = false;
};

// Returns true when this process was launched with the GameNative LSFG
// environment marker.  This must not infer LSFG from the executable name:
// GameNative intentionally supplies LSFG_PROCESS to identify the workload.
bool exynos_lsfg_process_active();

// Returns a point-in-time snapshot of the LSFG compatibility state.
LayerLsfgCompatSnapshot snapshot_lsfg_compat();

