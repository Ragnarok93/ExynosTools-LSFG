#pragma once

#include <atomic>
#include <cstdint>

enum ExynosLayerLogLevel : int {
    EXYNOS_LAYER_LOG_OFF = 0,
    EXYNOS_LAYER_LOG_WARN = 1,
    EXYNOS_LAYER_LOG_INFO = 2,
};

enum DepthFormatOverrideMode : int {
    DEPTH_OVERRIDE_NONE = 0,
    DEPTH_OVERRIDE_DISABLED = 1,
    DEPTH_OVERRIDE_SAFE = 2,
    DEPTH_OVERRIDE_AGGRESSIVE = 3,
};

struct LayerSettingsSnapshot {
    bool enabled = true;
    bool xclipse_only = true;
    bool bcn_intercept = true;
    bool force_bcn_emulation = true;
    bool strict_dispatch = false;
    bool drop_on_missing_commandbuffer_map = false;
    bool block_incompatible_virtual_copies = true;
    bool microbenchmark_enabled = false;
    bool cpu_decode_primary = true;
    int depth_override_mode = DEPTH_OVERRIDE_NONE;
    uint32_t disabled_bcn_mask = 0;
    uint32_t cpu_fallback_max_upload_mb = 128;
    int log_level = EXYNOS_LAYER_LOG_INFO;
};

struct LayerSettingsState {
    std::atomic<bool> enabled{true};
    std::atomic<bool> xclipse_only{true};
    std::atomic<bool> bcn_intercept{true};
    std::atomic<bool> force_bcn_emulation{true};
    std::atomic<bool> strict_dispatch{false};
    std::atomic<bool> drop_on_missing_commandbuffer_map{false};
    std::atomic<bool> block_incompatible_virtual_copies{true};
    std::atomic<bool> microbenchmark_enabled{false};
    std::atomic<bool> cpu_decode_primary{true};
    std::atomic<int> depth_override_mode{DEPTH_OVERRIDE_NONE};
    std::atomic<uint32_t> disabled_bcn_mask{0};
    std::atomic<uint32_t> cpu_fallback_max_upload_mb{128};
};
