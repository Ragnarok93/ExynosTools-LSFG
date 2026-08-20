# GameNative / Eden LSFG Integration Boundary

## Purpose

ExynosTools-LSFG is a Vulkan compatibility layer for Xclipse. Its BCn virtualization must remain available for applications that need it, but it should not compete with GameNative's native `lsfg-vk-android` frame-generation implementation.

## Verified GameNative architecture

GameNative includes `GameNative/lsfg-vk-android` as the `app/src/main/cpp/lsfg-vk-android` submodule. The Android port uses an independent Vulkan device for frame generation and shares images with the host through `AHardwareBuffer` rather than opaque file descriptors.

The frame-generation implementation creates its own Vulkan instance with:

- application name: `lsfg-vk-base`
- engine name: `lsfg-vk-base`
- Vulkan API: 1.3

Its Android image path imports caller-provided `AHardwareBuffer*` through `VK_ANDROID_external_memory_android_hardware_buffer` and keeps the shared image in `VK_IMAGE_LAYOUT_GENERAL`.

## Consequences for ExynosTools

1. **Do not remove BCn virtualization.** GameNative's BCn path and ExynosTools' fallback solve different compatibility problems.
2. **Do not decode GameNative's LSFG images.** The LSFG device's working images are normal color images supplied through AHB; BCn interception should naturally remain inactive for them.
3. **Avoid unnecessary BCn compute-runtime initialization for the LSFG internal device.** ExynosTools currently prewarms its BCn compute runtime for every detected Xclipse device. The GameNative LSFG device is a separate Xclipse Vulkan device and does not need this BCn prewarm.
4. **Preserve AHB/external-memory semantics.** The LSFG Android path depends on the AHB external-memory extension chain and dedicated imports. ExynosTools must not rewrite those requests as part of BCn virtualization.
5. **Keep LSFG frame-generation resources opaque to BCn routing.** Copy/blit interception should only take the special BCn path when the tracked image route actually involves a virtualized BCn image.

## Recommended next implementation slice

Add an explicit `is_lsfg_framegen_device` runtime classification using the instance application/engine names above. Use it first as a conservative policy gate to skip ExynosTools BCn compute prewarm on that internal device. Do not disable general Vulkan layer functionality, AHB support, synchronization, or image tracking.

After that change is verified, add diagnostics showing:

- LSFG internal-device detection;
- BCn virtual-image count on that device;
- BCn special-copy/decode hits on that device;
- whether any AHB-backed image entered a BCn virtualization route.

A successful GameNative integration should show an LSFG internal device with zero BCn virtualization hits while retaining the BCn fallback for ordinary application devices.

## Source evidence

The GameNative submodule is declared in GameNative's `.gitmodules` and points to `GameNative/lsfg-vk-android`.

The Android frame-generation repository documents the AHB-specific API and image path. Its `Device` implementation identifies the compute device independently and requires Android external-memory extensions; its `Image` implementation imports `AHardwareBuffer` directly.
