#!/usr/bin/env python3
from pathlib import Path

path = Path("src/layer/layer_entry.cpp")
text = path.read_text()
old = '''    // GameNative's LSFG internal device (app/engine name "lsfg-vk-base") only
    // performs frame generation on ordinary color images and never exercises the
    // BCn virtualization path. Skip the eager BCn compute-runtime prewarm for
    // it so ExynosTools does not allocate descriptor/command pools for a device
    // that will never decode a BCn texture. The lazy init path in
    // get_or_create_compute_runtime() remains available if a BCn image is ever
    // created on such a device. BCn virtualization for ordinary application
    // devices is unaffected.
    if (runtime.app.is_lsfg_framegen && runtime.is_xclipse) {
        EXYNOS_LOGI(
            "Skipping BCn compute runtime prewarm for LSFG frame-generation device "
            "(app='%s' engine='%s').",
            runtime.app.application_name.c_str(),
            runtime.app.engine_name.c_str());
    } else {
        prewarm_compute_runtime_if_needed(*pDevice, device_dispatch, runtime.is_xclipse);
    }
'''
new = '''    // GameNative lsfg-vk-android v1.3.3 runs frame generation on the game's
    // existing VkInstance/VkDevice (initializeExternal/createContextFromImages).
    // LSFG_PROCESS+LSFG_CONFIG therefore identifies the shared device more
    // reliably than the legacy "lsfg-vk-base" internal-device name. Avoid eager
    // BCn pipeline/descriptor allocation on that latency-sensitive device-create
    // path. BCn virtualization itself remains enabled and its runtime is still
    // initialized lazily if the game actually creates a virtual BCn image.
    // Keep the legacy name check for older GameNative/LSFG builds.
    const bool lsfg_shared_device = lsfg_process_active || runtime.app.is_lsfg_framegen;
    if (lsfg_shared_device && runtime.is_xclipse) {
        EXYNOS_LOGI(
            "Skipping eager BCn compute prewarm for GameNative LSFG shared device "
            "(env=%d legacyName=%d app='%s' engine='%s'); BCn remains lazy-enabled.",
            static_cast<int>(lsfg_process_active),
            static_cast<int>(runtime.app.is_lsfg_framegen),
            runtime.app.application_name.c_str(),
            runtime.app.engine_name.c_str());
    } else {
        prewarm_compute_runtime_if_needed(*pDevice, device_dispatch, runtime.is_xclipse);
    }
'''
if new in text:
    print("GameNative LSFG v1.3.3 patch already applied")
    raise SystemExit(0)
if old not in text:
    raise SystemExit("expected prewarm block not found; upstream source changed")
path.write_text(text.replace(old, new, 1))
print("patched", path)
