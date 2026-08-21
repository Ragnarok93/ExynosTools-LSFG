# ExynosTools-LSFG â€” Vortek Xclipse Vulkan Compatibility Layer

ExynosTools-LSFG is a Vulkan compatibility and performance layer for Samsung
Xclipse GPUs (Exynos / RDNA-based). It is a fork of
[WearyConcern1165/ExynosTools](https://github.com/WearyConcern1165/ExynosTools)
that adds a **GameNative LSFG-VK coexistence boundary** and **Android build
hardening** on top of the upstream BCn virtualization engine.

It is packaged so Android emulators and Winlator-style launchers can load it as
a graphics driver bundle, while internally it works as a Vulkan layer that sits
in front of the Samsung Vulkan driver (`vulkan.samsung.so`). Its job is to add
missing compatibility behavior, patch unsafe Vulkan edge cases, and provide
BCn texture virtualization paths for games and translation layers such as
DXVK and vkd3d-proton â€” without competing with GameNative's native
`lsfg-vk-android` frame generation.

> This layer does **not** implement frame generation, LSFG shaders, or LSFG
> configuration. GameNative already ships `lsfg-vk-android`. The objective of
> this fork is to make ExynosTools **coexist correctly** with that existing
> LSFG-VK implementation.

---

## Core Functionality

Inherited from upstream ExynosTools and retained unchanged:

- **Keeps the real Samsung Vulkan driver as the backend.** The layer intercepts
  selected Vulkan calls before they reach `vulkan.samsung.so`; it does not
  replace the driver stack.
- **Intercepts selected Vulkan calls.** Device-proc dispatch table hooks
  `CreateImage`/`DestroyImage`/`CreateImageView`, buffer-memory bind/map,
  `CreateGraphicsPipelines`, `CreateRenderPass`/`CreateFramebuffer`,
  `CreateSampler`, command pool/buffer, and the copy/blit family
  (`CmdCopyImage`/`2`/`KHR`, `CmdBlitImage`/`2`/`KHR`). It does **not** hook
  swapchain/present/acquire or queue-submit.
- **BCn virtualization.** BC4/BC5/BC6H/BC7 compatibility paths for Xclipse;
  BC1â€“BC3 remain native where the driver supports them. Includes sRGB/UNORM
  view remapping, safer mip/3D/cube/array handling, a CPU fallback upload path
  (`bcdec`), a compute decode pipeline, and runtime telemetry counters.
- **Image format compatibility.** Improved
  `vkGetPhysicalDeviceImageFormatProperties[2/KHR]` handling, conservative
  compatibility classes for common UNORM/SRGB formats,
  `VkImageFormatListCreateInfo` patching for mutable/block-compatible images,
  and safer pNext traversal/sanitization for virtualized images.
- **Android external memory / AHardwareBuffer.** Safer
  `VkAndroidHardwareBufferUsageANDROID` reporting; external-image queries for
  virtual BCn no longer incorrectly imply export/import support; external/AHB
  memory is blocked for virtualized BCn backing images when unsafe.
- **Depth/stencil safety.** Optional depth-format override infrastructure, safe
  `vkCreateImageView` remapping for reduced depth/stencil images, and safe
  `vkCmdClearDepthStencilImage` aspect-mask patching when stencil was removed
  by a reduced backing format.
- **Runtime / performance infrastructure.** VMA-based staging allocation,
  descriptor reuse/cache, pipeline cache and BCn decode prewarm, and
  `VK_KHR_synchronization2` where available.
- **Engine and driver quirks.** Detects application/engine info from
  `VkApplicationInfo` (DXVK, DXVK 2+, vkd3d-proton, clvk); uses `VkDriverId`
  where available; keeps Xclipse protected from non-Xclipse quirks; hides
  `VK_KHR_shader_float_controls` for Qualcomm and
  `VK_EXT_extended_dynamic_state*` for ARM/Mali outside Xclipse/DXVK-2+.

---

## New Features (this fork)

### GameNative LSFG-VK coexistence

A focused compatibility boundary so ExynosTools and GameNative's
`lsfg-vk-android` implicit layer run side by side without conflict:

- **LSFG process detection** (`src/layer/layer_lsfg_compat.{h,cpp}`): detects
  GameNative's LSFG workload by requiring **both** the `LSFG_PROCESS` and
  `LSFG_CONFIG` environment variables (the pair avoids false positives from
  inherited config). Exposes `exynos_lsfg_process_active()` and
  `snapshot_lsfg_compat()`.
- **External-memory image contract preservation**: in `layer_CreateImage`, BCn
  virtualization is bypassed for externally-backed BCn images (AHardwareBuffer
  or opaque-FD `VkExternalMemoryImageCreateInfo`) when LSFG is active. A virtual
  BCn image uses an internal decoded backing and cannot preserve an external
  handle, so this keeps LSFG's AHB/dedicated-import contract intact.
- **External-image query early-out**: `prepare_virtual_bcn_image_format_query()`
  forwards external-memory image queries to the driver unmodified (original
  format + handleType preserved), consistent with the create-path bypass.
- **Descriptor-buffer fast-path guard**: when LSFG is active, the
  descriptor-buffer fast path and its support query are skipped
  (`!lsfg_process_active`), so ExynosTools does not compete with LSFG's device
  setup.
- **LSFG internal-device classification**: `InstanceRuntime::is_lsfg_framegen`
  detects the LSFG frame-generation device by exact match on its instance
  app/engine name `lsfg-vk-base`. `layer_CreateDevice` then skips the eager BCn
  compute-runtime prewarm for that device (it only runs framegen on RGBA8/RGBA16F
  images and never exercises BCn), while keeping the BCn fallback fully active
  for ordinary application devices.
- **External FD dispatch entries**: `DeviceDispatch` extended with
  `AllocateMemory`/`FreeMemory`, `GetMemoryFd[KHR]`,
  `Create/DestroySemaphore`, `Get/ImportSemaphoreFd[KHR]`,
  `Create/DestroyFence`, `Get/ImportFenceFd[KHR]` for clean pass-through of
  external-memory and sync FD paths.

Why this is safe: LSFG's working images are ordinary color (RGBA8/RGBA16F)
images, never BCn, so BCn virtualization naturally never touches them, and
ExynosTools does not hook the swapchain/present/queue-submit path LSFG owns.
Both layers are GLOBAL implicit with only a disable-environment, so the
coexistence is order-insensitive. See
[`docs/LSFG_GAMENATIVE_INTEGRATION.md`](docs/LSFG_GAMENATIVE_INTEGRATION.md).

### Android-native Vulkan vendor probe

`tests/android_native_probe/` is a standalone probe APK used to
reverse-engineer the Xclipse 940 driver's external-memory, AHardwareBuffer,
format, image-capability, interpolation, and motion behavior, so the
compatibility layer can be tuned to real hardware limits.

---

## Improvements (this fork)

### Android ELF build hardening

The most important non-LSFG change â€” prevents build-machine paths from leaking
into the shipped Android layer ELF:

- `CMAKE_SKIP_RPATH ON`, plus `SKIP_BUILD_RPATH`/`BUILD_WITH_INSTALL_RPATH OFF`/
  `INSTALL_RPATH ""`/`SKIP_INSTALL_RPATH ON`.
- Android system libraries (`vulkan`, `log`) linked **by soname** instead of
  absolute NDK paths.
- A `patchelf --remove-rpath` **post-build** step strips the Termux-Clang NDK
  sysroot `DT_RUNPATH` from the final `.so`.
- Vulkan-Utility-Libraries include/src discovery restructured to be overridable
  via `EXYNOS_VULKAN_UTILITY_INCLUDE_DIR` (with a fallback `../src/vulkan`
  resolution).
- Removed the `exports.map` version script.

### Repository hygiene

- Untracked **221+ committed scratch/build artifacts** (a full
  `build-lsfg-termux/` directory, `.bak` backups, prebuilt `.so`/`.zip`
  bundles, probe capture dumps, stray pipe-named files, logs).
- Expanded `.gitignore` to keep build dirs, `*.so`/`*.zip`/`*.log`, phase
  backups, and probe scratch from being re-committed.
- The reviewable delta is now just the real source changes
  (`layer_lsfg_compat.{cpp,h}`, `layer_entry.cpp`,
  `layer_device_dispatch_types.h`), `CMakeLists.txt`, the docs, and the probe
  project.

---

## Target Hardware

Samsung Xclipse:

- Xclipse 920, 940, 950, 960
- Xclipse 530, 540, 550
- Future Xclipse/RDNA-derived Samsung GPUs where the same Vulkan behavior applies

The layer detects Xclipse devices by device name and vendor/driver information.
Some generic Android-driver quirks are included, but the main compatibility path
is designed for Samsung Xclipse.

---

## Driver Bundle Layout

A typical packaged driver zip contains:

```text
meta.json
VkLayer_vortek_xclipse.json
vulkan.samsung.so
libVkLayer_VortekXclipse.so
```

- `vulkan.samsung.so` â€” the real backend driver.
- `libVkLayer_VortekXclipse.so` â€” the ExynosTools/Vortek compatibility layer.
- `meta.json` + `VkLayer_vortek_xclipse.json` â€” let supported launchers discover
  and load the package.

No `.ini` sidecar is required in current builds; runtime configuration is via
Vulkan layer settings or built-in defaults.

The layer manifest (`VkLayer_vortek_xclipse.json`) is a GLOBAL implicit layer
with disable-environment `DISABLE_VORTEK_XCLIPSE_LAYER=1` and Vulkan API 1.3.

---

## Build

Built for Android ARM64 (AArch64) with CMake and the Android NDK. Provide the
Vulkan dependencies either as submodules in `external/` or via
`EXYNOS_VULKAN_REPOS_ROOT` / `EXYNOS_VULKAN_HEADERS_PATH`:

- Vulkan-Headers
- Vulkan-Utility-Libraries
- VulkanMemoryAllocator

> Pin Vulkan-Utility-Libraries to a tag matching your Vulkan headers
> (e.g. `v1.4.341` for `VK_HEADER_VERSION` 341); VUL `main` can be ahead of
> older system headers and fail to compile.

### Termux / Android (aarch64)

```bash
cmake -S . -B build-lsfg-termux \
  -DEXYNOS_LAYER_USE_SUBMODULE_DEPS=ON \
  -DEXYNOS_LAYER_USE_LOCAL_VULKAN_REPOS=OFF \
  -DEXYNOS_LAYER_BUILD_SHADERS=ON \
  -DEXYNOS_LAYER_EMBED_SHADERS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-lsfg-termux --target VkLayer_VortekXclipse -j$(nproc)
```

Output: `build-lsfg-termux/libVkLayer_VortekXclipse.so`

Verify the ELF (expect `Class: ELF64`, `Type: DYN`, `Machine: AArch64`, and
exported `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`):

```bash
readelf -h build-lsfg-termux/libVkLayer_VortekXclipse.so | grep -E 'Class:|Type:|Machine:'
readelf -Ws build-lsfg-termux/libVkLayer_VortekXclipse.so | \
  grep -E 'vkGetInstanceProcAddr|vkGetDeviceProcAddr'
```

### Standard NDK (host)

```bash
cmake --fresh -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android --config Release -j 8
```

---

## Credits

Builds on the upstream
[WearyConcern1165/ExynosTools](https://github.com/WearyConcern1165/ExynosTools),
which in turn builds on ideas and code patterns from:

- [leegao/bionic-vulkan-wrapper](https://github.com/leegao/bionic-vulkan-wrapper)
  â€” Android Vulkan wrapper ideas, driver quirks, depth/stencil safety,
  AHardwareBuffer/external-memory handling, format compatibility.
- Vortek by brunodev85 â€” the original Vortek ecosystem and Vulkan compatibility
  direction.
- Granite by Themaister â€” BCn compute shader inspiration.
- The broader Mesa/Vulkan ecosystem â€” reference behavior around formats, pNext
  chains, and driver compatibility.

GameNative's `lsfg-vk-android` is referenced as a read-only compatibility
target; this layer does not redistribute or re-implement it.

---

## Status

Experimental compatibility layer for testing on Xclipse devices. It is not a
replacement for the Samsung kernel driver or the full Vulkan driver stack â€” it
wraps and improves selected behavior around the existing Samsung Vulkan driver.

Use it for testing, debugging, and compatibility work. Some games may still
require per-title fixes.