#pragma once

#include <atomic>

#include "layer_settings_types.h"

#if defined(__ANDROID__)
#include <android/log.h>
#else
#include <cstdio>
#endif

inline std::atomic<int>& exynos_layer_log_level_storage() {
    static std::atomic<int> level{EXYNOS_LAYER_LOG_INFO};
    return level;
}

inline bool exynos_layer_should_log_warn() {
    return exynos_layer_log_level_storage().load(std::memory_order_relaxed) >= EXYNOS_LAYER_LOG_WARN;
}

inline bool exynos_layer_should_log_info() {
    return exynos_layer_log_level_storage().load(std::memory_order_relaxed) >= EXYNOS_LAYER_LOG_INFO;
}

#if defined(__ANDROID__)
#define EXYNOS_LOGI(...) do { if (exynos_layer_should_log_info()) __android_log_print(ANDROID_LOG_INFO, "VortekXclipse", __VA_ARGS__); } while (0)
#define EXYNOS_LOGW(...) do { if (exynos_layer_should_log_warn()) __android_log_print(ANDROID_LOG_WARN, "VortekXclipse", __VA_ARGS__); } while (0)
#else
#define EXYNOS_LOGI(...) do { if (exynos_layer_should_log_info()) { std::fprintf(stderr, "[VortekXclipse][I] " __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)
#define EXYNOS_LOGW(...) do { if (exynos_layer_should_log_warn()) { std::fprintf(stderr, "[VortekXclipse][W] " __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)
#endif
