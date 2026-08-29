# GameNative 1.2.0 LSFG Integration Boundary

## Hard compatibility target

ExynosTools-LSFG targets the **unmodified stock GameNative v1.2.0 codebase** at commit `3491226faedb7222a5f8b7248c0247957a060836`.

GameNative's existing Wrapper components, Contents Manager behavior, Bionic launcher, LSFG manager, `liblsfg-vk-layer.so`, manifests, configuration flow, and application functionality are compatibility constraints. They are not implementation targets and must not be patched or replaced.

GameNative 1.2.0 bundles an LSFG runtime identified by `LsfgVkManager` as `v1.3.3-android-arm64-v8a`. The app version and bundled LSFG runtime version are separate facts.

## Correct load chain

The intended Vulkan chain is:

```text
Game / DXVK / vkd3d-proton
        ↓
GameNative's stock VK_LAYER_LS_frame_generation
        ↓
GameNative's stock Wrapper component
        ↓
libvulkan_exynostools.so        (custom driver selected through AdrenoTools)
        ↓
libVkLayer_VortekXclipse.so     (ExynosTools compatibility engine, hosted by the shim)
        ↓
/system/lib64/libvulkan.so
        ↓
Samsung/Xclipse vendor ICD
```

The important distinction is that **ExynosTools is the custom driver underneath the existing Wrapper**. It is not a replacement Wrapper WCP.

Stock GameNative's `AdrenotoolsManager` already provides the required driver-injection contract. For a selected custom driver it reads `meta.json` and sets:

- `ADRENOTOOLS_DRIVER_PATH`
- `ADRENOTOOLS_HOOKS_PATH`
- `ADRENOTOOLS_DRIVER_NAME`

The selected stock Wrapper then loads the named custom Vulkan library. ExynosTools therefore packages `libvulkan_exynostools.so` as `libraryName` in `meta.json`.

## Why a driver shim is required

The original prototype ZIP set `libraryName=vulkan.samsung.so` but did not contain `vulkan.samsung.so`. GameNative's stock AdrenoTools path therefore had no loadable library corresponding to the metadata. Merely placing `libVkLayer_VortekXclipse.so` and its manifest in the custom-driver directory also does not make that directory part of GameNative's Vulkan layer search path.

`libvulkan_exynostools.so` closes that gap without changing GameNative:

1. GameNative's existing Wrapper loads the shim through its normal AdrenoTools path.
2. The shim opens Android's system Vulkan loader at `/system/lib64/libvulkan.so` (or `/system/lib/libvulkan.so` on 32-bit builds), matching the stock Wrapper's system-driver fallback model.
3. The shim loads the packaged `libVkLayer_VortekXclipse.so` from `ADRENOTOOLS_DRIVER_PATH`.
4. It supplies the standard Vulkan loader-link structures expected by the ExynosTools layer for `vkCreateInstance` and `vkCreateDevice`.
5. The existing ExynosTools layer retains its normal instance/device dispatch maps and compatibility logic while the real downstream implementation remains Samsung Vulkan.

No proprietary Samsung driver binary is redistributed or renamed.

## LSFG ownership

GameNative's stock LSFG layer remains solely responsible for frame generation and presentation interception.

ExynosTools must not replace or take ownership of:

- `vkCreateSwapchainKHR`
- `vkDestroySwapchainKHR`
- `vkAcquireNextImageKHR` / `vkAcquireNextImage2KHR`
- `vkQueuePresentKHR`
- LSFG frame-generation contexts or working images
- GameNative's `Lossless.dll`, `conf.toml`, manifest, or LSFG lifecycle

GameNative 1.2.0 arms LSFG with `LSFG_PROCESS=gamenative-lsfg` and a non-empty `LSFG_CONFIG`. ExynosTools uses that pair only as a compatibility/coexistence signal.

## ExynosTools LSFG coexistence policy

When the stock GameNative LSFG environment is active:

- preserve the real Xclipse feature/extension contract; never fabricate an LSFG-required capability;
- skip optional descriptor-buffer injection that could alter the shared-device feature setup;
- skip eager BCn compute-runtime prewarm on the LSFG shared-device path;
- preserve external-memory/AHardwareBuffer contracts and bypass BCn virtualization for externally-backed BCn images;
- keep LSFG's RGBA8/RGBA16F working images outside the BCn virtualization path;
- keep ordinary BCn virtualization available to the game when it actually uses unsupported BC formats;
- preserve feature and synchronization pNext chains rather than replacing them.

The current ExynosTools compatibility layer intentionally does not hook the LSFG swapchain/present path.

## Stock GameNative 1.2.0 contracts used

The compatibility tests pin and verify all of these behaviors in the exact 1.2.0 source tree:

- stock Wrapper components remain present in `graphics_driver_download.json`, including `wrapper-gamenative`, `wrapper-v2`, and `wrapper-legacy`;
- `XServerScreen` retains normal Wrapper selection and invokes `AdrenotoolsManager.setDriverById` for a non-System custom driver;
- `AdrenotoolsManager` reads a custom driver's `libraryName` and publishes the existing `ADRENOTOOLS_*` environment contract;
- the Bionic launcher propagates the container environment without ExynosTools-specific GameNative changes;
- `LsfgVkManager.applyLaunchEnv` remains GameNative-owned and adds its stock LSFG layer/configuration environment;
- GameNative continues to install and own `liblsfg-vk-layer.so` and `VkLayer_LS_frame_generation.json`.

## Package contract

The GameNative-installable ExynosTools artifact is a normal root-flat custom-driver ZIP, not a WCP. Required runtime members are:

```text
meta.json
libvulkan_exynostools.so
libVkLayer_VortekXclipse.so
```

`VkLayer_vortek_xclipse.json` may also be included for standalone/debug layer use, but the GameNative driver path does not depend on Vulkan implicit-layer discovery for ExynosTools itself: the shim hosts the layer directly.

The package must not contain:

```text
profile.json
libvulkan_wrapper.so
wrapper_icd.aarch64.json
```

Those belong to GameNative's Wrapper layer and are deliberately untouched.

## Validation

`tests/gamenative_120_stock_wrapper_contract.py` validates the exact stock GameNative 1.2.0 Wrapper/AdrenoTools/LSFG source contract and optionally validates a built custom-driver ZIP.

`tests/lsfg_compat_contract.py` validates the ExynosTools LSFG coexistence rules and the pinned stock GameNative LSFG manager contract.

`tests/termux_lsfg/run.sh` can additionally validate a local exact GameNative 1.2.0 checkout with `GAMENATIVE_120_ROOT` and a built custom-driver ZIP with `GAMENATIVE_120_DRIVER_ZIP`.

`.github/workflows/lsfg-compat.yml` builds both ARM64 runtime libraries, packages the custom driver, and validates it against the untouched GameNative 1.2.0 source tree.

The final proof still requires a physical Xclipse device because Android linker namespaces and the Samsung vendor ICD cannot be completely represented by a raw Termux process.

## End-to-end acceptance criteria

A stock GameNative 1.2.0 device run is accepted only when:

1. an unchanged stock Wrapper is selected;
2. the ExynosTools custom driver is selected through GameNative's existing driver-version/AdrenoTools path;
3. `libvulkan_exynostools.so` loads `libVkLayer_VortekXclipse.so` and reaches Android system Vulkan without recursion;
4. the resulting physical device is the Samsung/Xclipse vendor implementation;
5. GameNative's stock `VK_LAYER_LS_frame_generation` loads normally and `LSFG_PROCESS` + `LSFG_CONFIG` are present;
6. device creation succeeds with LSFG's required feature chain intact;
7. LSFG creates and uses its frame-generation context without device loss or a watchdog stall;
8. measured presented FPS rises above base rendered FPS for a multiplier greater than 1;
9. generated frames remain visually coherent through motion and scene changes;
10. BCn-dependent games still use ExynosTools compatibility paths where needed;
11. disabling LSFG restores normal presentation while leaving the same stock Wrapper and ExynosTools custom driver selected.
