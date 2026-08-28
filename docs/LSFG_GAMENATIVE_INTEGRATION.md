# GameNative LSFG Integration Boundary

## Compatibility target: stock GameNative 1.2.0

The compatibility target for ExynosTools-LSFG is the **unmodified GameNative app v1.2.0 codebase** at commit `3491226faedb7222a5f8b7248c0247957a060836`.

That GameNative 1.2.0 codebase downloads/installs an LSFG runtime identified by its manager as `v1.3.3-android-arm64-v8a`. The runtime version string must not be confused with the GameNative app version: ExynosTools must continue to install and operate through the Wrapper interface already implemented by stock GameNative 1.2.0.

GameNative 1.2.0 arms its LSFG Vulkan layer with `LSFG_PROCESS=gamenative-lsfg` and a non-empty `LSFG_CONFIG`. ExynosTools treats that environment pair as the authoritative process marker. The older `lsfg-vk-base` application/engine-name classification is retained only for compatibility with older LSFG builds.

The ExynosTools WCP replaces **only GameNative's Wrapper Vulkan component**. Stock GameNative remains responsible for downloading/configuring `liblsfg-vk-layer.so`, installing `VkLayer_LS_frame_generation.json`, setting `VK_LAYER_PATH`, and launching the Bionic process with the LSFG environment.

## Bundled LSFG runtime architecture

The LSFG runtime bundled by stock GameNative 1.2.0 uses a **single-device frame-generation** path. It does not require ExynosTools to create an independent frame-generation Vulkan device, and ExynosTools must not take ownership of LSFG swapchain/present objects.

The LSFG layer:

1. hooks the game's Vulkan instance/device and swapchain;
2. preserves/enables the supported storage-image, FP16 and Vulkan-memory-model features it needs in the game's `VkDeviceCreateInfo`;
3. creates RGBA working images on the game's existing `VkDevice` with storage, sampled, transfer-source and transfer-destination usage;
4. initializes frame generation against the game's instance, physical device, device, queue family and queue;
5. creates the frame-generation context from those same-device images;
6. runs frame generation on the shared device/queue and owns the presentation interception path.

The runtime also checks external-memory/external-semaphore capabilities during setup, so ExynosTools must expose the real Xclipse extension contract unchanged.

## ExynosTools policy for LSFG

ExynosTools must remain transparent to LSFG's device requirements while retaining BCn virtualization for the game itself.

- Never fabricate an LSFG-required feature. Forward the Samsung/Xclipse feature result and preserve GameNative/LSFG feature-enable pNext structures.
- Never hide Xclipse extensions required by GameNative. Xclipse devices are excluded from the non-Xclipse extension-hide quirks.
- Do not inject optional descriptor-buffer behavior into an LSFG-marked process. GameNative/LSFG owns the shared-device feature contract.
- Skip eager BCn compute-runtime prewarm when `LSFG_PROCESS` + `LSFG_CONFIG` identify the shared LSFG/game device. BCn remains lazy-enabled if the game later uses a virtual BC format.
- Do not virtualize externally-backed BCn images in an LSFG process. External-memory image contracts must stay native.
- Ordinary LSFG working images (`R16G16B16A16_SFLOAT` or `R8G8B8A8_UNORM`) are not BC formats and therefore naturally bypass BCn virtualization.
- Copy/blit interception may route through ExynosTools' special path only when tracked images actually involve a virtual BCn image.
- Do not implement or replace `vkCreateSwapchainKHR` / `vkQueuePresentKHR` ownership for LSFG. The GameNative-provided LSFG layer owns those hooks.

## Wrapper WCP boundary

Stock GameNative 1.2.0 already supports a custom `Wrapper` content profile. The ExynosTools WCP intentionally stays minimal:

- `libvulkan_wrapper.so`
- `libadrenotools.so`
- `wrapper_icd.aarch64.json`
- `profile.json`

The wrapper must preserve the LSFG feature pNext chain during `vkCreateDevice`. If an optional compatibility retry would require replacing that chain with `pNext = NULL`, it must fail rather than silently stripping LSFG-required features. Optional ExynosTools GPU BCn compute-transcode initialization is also disabled in an LSFG-marked process so ExynosTools does not inject unrelated compute work into LSFG's shared presentation device; CPU BCn fallback remains available.

## Xclipse 940 evidence already captured

The repository's device-validation history recorded Samsung Xclipse 940 / Vulkan 1.3.279 exposing the capabilities used by the bundled LSFG path:

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

Those observations establish that the raw Xclipse 940 driver is not missing the fundamental Vulkan capabilities required by the bundled LSFG path. They do not replace an end-to-end GameNative presentation test.

## Validation gates

`tests/lsfg_compat_contract.py` validates the ExynosTools source contract. With `--gamenative-root`, it additionally requires the GameNative tree to resolve to the exact stock v1.2.0 SHA and checks the LSFG manager/launcher contract from that pinned tree. `--lsfg-root` remains available only as an explicit developer check against a separately supplied LSFG source tree; it is not the authoritative GameNative 1.2.0 compatibility anchor.

`.github/workflows/lsfg-compat.yml` fetches the exact GameNative v1.2.0 commit directly and refuses revision drift. It deliberately does not clone mutable GameNative `main` or a mutable LSFG `release` branch for the stock-1.2.0 compatibility decision.

`tests/gamenative_120_wcp_contract.py` validates the generated WCP against the same exact GameNative source revision and verifies the stock Wrapper trust/application contract plus stock LSFG ownership.

`tests/termux_lsfg/run.sh` always validates the LSFG environment state machine and source contract. Set `GAMENATIVE_120_ROOT` to a local checkout of the exact stock GameNative 1.2.0 revision to enable the pinned app-source check. Set `GAMENATIVE_120_WCP` as well to validate a built ExynosTools WCP against that checkout.

The Samsung vendor ICD itself must be exercised from an Android application process because Android linker namespaces can prevent a raw Termux process from loading vendor Vulkan dependencies. `tests/android_native_probe` remains the native-Android ICD/layer validation path.

## End-to-end acceptance criteria

A device run is considered LSFG-compatible only when all of these are observed on an Xclipse device:

1. baseline Samsung Vulkan instance/device creation succeeds;
2. the ExynosTools Wrapper loads the Samsung Vulkan implementation without self-recursion;
3. stock GameNative 1.2.0 discovers and loads `VK_LAYER_LS_frame_generation` while the ExynosTools Wrapper remains the selected Vulkan component;
4. `LSFG_PROCESS` and `LSFG_CONFIG` are present in the game process;
5. LSFG creates its shared-device frame-generation context without `VK_ERROR_FEATURE_NOT_PRESENT`, `VK_ERROR_EXTENSION_NOT_PRESENT`, `VK_ERROR_DEVICE_LOST`, or a watchdog stall;
6. generated-frame presentation increases measured output FPS above base FPS for multiplier > 1;
7. frame-generation output remains visually coherent through motion and scene changes;
8. BCn titles still use ExynosTools virtualization when required and LSFG color working images never enter the BCn route;
9. disabling LSFG restores normal game presentation without requiring a different ExynosTools package.

Passing CI proves pinned source/build compatibility. Passing the Android-native probe proves Samsung ICD/layer execution. Only a stock GameNative 1.2.0 device run proves end-to-end frame generation.
