# ExynosTools Xclipse Vulkan Compatibility Layer

ExynosTools is a Vulkan compatibility and performance layer for Samsung Xclipse GPUs.

The project is packaged so Android emulators and Winlator-style launchers can load it as a graphics driver bundle, while internally it works as a Vulkan layer that sits in front of the Samsung Vulkan driver (`vulkan.samsung.so`). Its job is to add missing compatibility behavior, patch unsafe Vulkan edge cases, and provide BCn texture virtualization paths for games and translation layers such as DXVK and vkd3d-proton.

This repository contains the source code for the current Vortek/ExynosTools layer branch focused on Xclipse compatibility.

## What It Does

- Keeps the real Samsung Vulkan driver as the backend.
- Intercepts selected Vulkan calls before they reach the driver.
- Reports missing or virtualized BCn texture support where needed.
- Decodes/virtualizes unsupported BCn formats through CPU/compute paths depending on runtime policy.
- Improves compatibility with DXVK, vkd3d-proton, Winlator forks, and Android Vulkan emulators.
- Adds safety patches for fragile Vulkan structures, pNext chains, format queries, image views, and depth/stencil operations.

## Target Hardware

The main target is Samsung Xclipse hardware:

- Xclipse 920
- Xclipse 930
- Xclipse 940
- Future Xclipse/RDNA-derived Samsung GPUs where the same Vulkan behavior applies

The layer detects Xclipse devices by device name and vendor/driver information. Some generic Android-driver quirks are included, but the main compatibility path is designed for Samsung Xclipse.

## Driver Bundle Layout

A typical packaged driver zip contains:

```text
meta.json
VkLayer_vortek_xclipse.json
vulkan.samsung.so
libVkLayer_VortekXclipse.so
```

`vulkan.samsung.so` is the real backend driver.

`libVkLayer_VortekXclipse.so` is the ExynosTools/Vortek compatibility layer.

`meta.json` and `VkLayer_vortek_xclipse.json` allow supported launchers/emulators to discover and load the package.

No `.ini` sidecar file is required in the current builds. Runtime configuration is intended to happen through Vulkan layer settings or built-in defaults.

## Current Features

### BCn Virtualization

- BC4, BC5, BC6H, and BC7 compatibility paths for Xclipse.
- BC1-BC3 can remain native where supported by the driver.
- sRGB/UNORM view remapping for virtualized BCn images.
- Safer handling of mip levels, 3D textures, cube compatibility, and array layers.
- CPU fallback path for texture uploads.
- Runtime tracking and telemetry counters for virtualized formats.

### Image Format Compatibility

- Improved `vkGetPhysicalDeviceImageFormatProperties` handling.
- Improved `vkGetPhysicalDeviceImageFormatProperties2/KHR` handling.
- Conservative compatibility classes for common UNORM/SRGB image formats.
- `VkImageFormatListCreateInfo` patching for mutable and block-compatible images.
- Safer pNext traversal and sanitization for virtualized images.

### Android External Memory / AHardwareBuffer

- Safer `VkAndroidHardwareBufferUsageANDROID` reporting.
- External image queries for virtual BCn no longer incorrectly imply export/import support.
- External/AHB memory is blocked for virtualized BCn backing images when unsafe.

### Depth/Stencil Safety

- Optional depth format override infrastructure.
- Safe `vkCreateImageView` remapping for reduced depth/stencil images.
- Safe `vkCmdClearDepthStencilImage` aspect-mask patching when stencil was removed by a reduced backing format.

### Runtime / Performance Infrastructure

- VMA-based staging allocation support.
- Descriptor reuse/cache infrastructure.
- Pipeline cache and basic prewarm support for BCn decode pipelines.
- Synchronization2 support where available.

### Engine And Driver Quirks

- Detects application/engine information from `VkApplicationInfo`.
- Detects DXVK, DXVK 2+, vkd3d-proton, and clvk.
- Uses `VkDriverId` where available.
- Keeps Xclipse protected from non-Xclipse quirks.
- Qualcomm quirk: hides `VK_KHR_shader_float_controls` outside Xclipse.
- ARM/Mali quirk: hides `VK_EXT_extended_dynamic_state*` for non-DXVK-2+ paths outside Xclipse.

## Build Notes

This project is built for Android ARM64 with CMake and the Android NDK.

This clean repository snapshot intentionally does not vendor the full Vulkan dependency trees, because they contain thousands of files. Before building, provide these dependencies either as submodules in `external/` or through `EXYNOS_VULKAN_REPOS_ROOT` / `EXYNOS_VULKAN_HEADERS_PATH`:

- Vulkan-Headers
- Vulkan-Utility-Libraries
- VulkanMemoryAllocator

The packaged test zips can include compiled binaries, but the source repository should normally keep those binaries out of Git.

Example configuration:

```powershell
cmake --fresh -S . -B build-android -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME\build\cmake\android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-26 `
  -DCMAKE_BUILD_TYPE=Release
```

Example build:

```powershell
cmake --build build-android --config Release -j 8
```

The main output is:

```text
build-android/libVkLayer_VortekXclipse.so
```

## Credits

This project builds on ideas and code patterns from several Vulkan compatibility projects.

Special thanks and credit to:

- [leegao/bionic-vulkan-wrapper](https://github.com/leegao/bionic-vulkan-wrapper) for Android Vulkan wrapper ideas, driver quirks, depth/stencil safety concepts, AHardwareBuffer/external-memory handling patterns, and format compatibility inspiration.
- Vortek by brunodev85 for the original Vortek ecosystem and Vulkan compatibility direction.
- Granite by Themaister for BCn compute shader inspiration.
- The broader Mesa/Vulkan ecosystem for reference behavior around formats, pNext chains, and driver compatibility.

## Status

This is an experimental compatibility layer intended for testing on Xclipse devices.

It is not a replacement for the Samsung kernel driver or the full Vulkan driver stack. It wraps and improves selected behavior around the existing Samsung Vulkan driver.

Use it for testing, debugging, and compatibility work. Some games may still require per-title fixes.
