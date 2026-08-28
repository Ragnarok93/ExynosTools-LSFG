# GameNative LSFG Integration Boundary

## Current compatibility target

This document tracks the Android LSFG runtime currently shipped by GameNative as `v1.3.3-android-arm64-v8a` and the current `GameNative/lsfg-vk-android` release architecture.

GameNative arms the Vulkan layer with `LSFG_PROCESS=gamenative-lsfg` and a non-empty `LSFG_CONFIG`. ExynosTools treats that environment pair as the authoritative process marker. The old `lsfg-vk-base` application/engine-name classification is retained only for compatibility with older LSFG builds.

## GameNative v1.3.3 architecture

The current Android path is **single-device frame generation**. It does not create an independent frame-generation Vulkan device and it does not use AHardwareBuffer as the primary working-image bridge.

On Android, `lsfg-vk-android`:

1. hooks the game's Vulkan instance/device and swapchain;
2. merges the supported storage-image, FP16 and Vulkan-memory-model features needed by framegen into the game's `VkDeviceCreateInfo`;
3. creates RGBA working images directly on the game's existing `VkDevice` with storage, sampled, transfer-source and transfer-destination usage;
4. calls `LSFG_3_1{P}::initializeExternal(...)` with the game's instance, physical device, device, queue family and queue;
5. calls `createContextFromImages(...)` with those device-local images;
6. runs frame generation on that same device/queue and copies generated output back into the game presentation path.

GameNative still requests/filter-checks external-memory/external-semaphore FD extensions in its layer setup, so ExynosTools must continue to expose the real Xclipse extension contract unchanged.

## ExynosTools policy for LSFG

ExynosTools must remain transparent to LSFG's device requirements while retaining BCn virtualization for the game itself.

- Never fabricate an LSFG-required feature. Forward the Samsung/Xclipse feature result and preserve GameNative's feature-enable pNext structures.
- Never hide Xclipse extensions required by GameNative. Xclipse devices are excluded from the non-Xclipse extension-hide quirks.
- Do not inject optional descriptor-buffer behavior into an LSFG-marked process. GameNative owns the shared device feature contract.
- Skip eager BCn compute-runtime prewarm when `LSFG_PROCESS` + `LSFG_CONFIG` identify the shared LSFG/game device. BCn remains lazy-enabled if the game later uses a virtual BC format.
- Do not virtualize externally-backed BCn images in an LSFG process. External-memory image contracts must stay native.
- Ordinary LSFG working images (`R16G16B16A16_SFLOAT` or `R8G8B8A8_UNORM`) are not BC formats and therefore naturally bypass BCn virtualization.
- Copy/blit interception may only route through ExynosTools' special path when tracked images actually involve a virtual BCn image.

## Xclipse 940 evidence already captured

The repository's device-validation history recorded Samsung Xclipse 940 / Vulkan 1.3.279 exposing the requirements used by GameNative v1.3.3:

- `shaderStorageImageExtendedFormats = YES`
- `shaderStorageImageReadWithoutFormat = YES`
- `shaderStorageImageWriteWithoutFormat = YES`
- `shaderInt16 = YES`
- Vulkan 1.2/1.3 support including timeline semaphore and synchronization2
- `VK_KHR_shader_float16_int8`
- `VK_KHR_vulkan_memory_model`
- `VK_KHR_external_memory` and `VK_KHR_external_memory_fd`
- `VK_KHR_external_semaphore` and `VK_KHR_external_semaphore_fd`
- OPAQUE_FD memory import/export support
- OPAQUE_FD and SYNC_FD semaphore import/export support

Those observations establish that the raw Xclipse 940 driver is not missing the fundamental Vulkan capabilities required by the current LSFG path. They do not replace an end-to-end GameNative presentation test.

## Validation gates

`tests/lsfg_compat_contract.py` checks the source-level contract against the current upstream GameNative LSFG release and GameNative manager. CI deliberately clones those upstream repositories so a future architecture or environment-variable change breaks the compatibility check instead of silently drifting.

`tests/termux_lsfg/run.sh` validates the LSFG environment state machine and an existing Termux-built layer ELF. The Samsung vendor ICD itself must be exercised from an Android application process because Android linker namespaces can prevent a raw Termux process from loading vendor Vulkan dependencies. `tests/android_native_probe` remains the native-Android ICD/layer validation path.

## End-to-end acceptance criteria

A device run is considered LSFG-compatible only when all of these are observed on an Xclipse device:

1. baseline Samsung Vulkan instance/device creation succeeds;
2. ExynosTools layer creation succeeds with the Samsung ICD as backend;
3. GameNative discovers and loads `VK_LAYER_LS_frame_generation` together with ExynosTools;
4. `LSFG_PROCESS` and `LSFG_CONFIG` are present in the game process;
5. GameNative creates its single-device framegen context without `VK_ERROR_FEATURE_NOT_PRESENT`, `VK_ERROR_EXTENSION_NOT_PRESENT`, `VK_ERROR_DEVICE_LOST`, or a watchdog stall;
6. generated-frame presentation increases measured output FPS above base FPS for multiplier > 1;
7. framegen output remains visually coherent through motion and scene changes;
8. BCn titles still use ExynosTools virtualization when required and LSFG color working images never enter the BCn route;
9. disabling LSFG restores normal game presentation without requiring a different ExynosTools package.

Passing CI proves source/build compatibility. Passing the Android-native probe proves Samsung ICD/layer execution. Only the final GameNative device run proves end-to-end frame generation.