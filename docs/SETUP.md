# Setup

ExynosTools-LSFG has two build products. Set up only the one you are working on.

## General emulator / Vortek Vulkan layer

Requirements:

- Android NDK
- CMake + Ninja
- `glslc`
- `patchelf`
- Vulkan-Headers
- Vulkan-Utility-Libraries
- VulkanMemoryAllocator headers

CI pins Vulkan-Headers and Vulkan-Utility-Libraries to `v1.4.341` and uses the
VulkanMemoryAllocator copy already present in `external/`.

For a local build, place matching Vulkan-Headers and Vulkan-Utility-Libraries
checkouts under:

```text
external/Vulkan-Headers
external/Vulkan-Utility-Libraries
```

then configure:

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release \
  -DEXYNOS_LAYER_USE_SUBMODULE_DEPS=ON \
  -DEXYNOS_LAYER_USE_LOCAL_VULKAN_REPOS=OFF \
  -DEXYNOS_LAYER_BUILD_SHADERS=ON \
  -DEXYNOS_LAYER_EMBED_SHADERS=ON

cmake --build build-android --target VkLayer_VortekXclipse -j2
```

The output is `build-android/libVkLayer_VortekXclipse.so`.

Use `scripts/package_emulator_driver.py` to package it. A complete standalone
emulator bundle requires the matching Samsung `vulkan.samsung.so` backend;
layer-only packages are for CI or launchers that already supply that backend.

## GameNative LSFG Wrapper

The GameNative product is built by
`.github/workflows/build-gamenative-wrapper.yml`. It intentionally uses the
pinned `leegao/mesa-wrapper-CI` source and produces a GameNative `Wrapper` WCP.
It does not compile `src/layer/` and must not be used to validate the general
Vortek layer.

See `docs/LSFG_GAMENATIVE_INTEGRATION.md` for that integration boundary.

## Validation rule

Before game testing, prove which product was loaded:

- General emulator path: `VK_LAYER_VORTEK_XCLIPSE` /
  `libVkLayer_VortekXclipse.so`.
- GameNative path: Wrapper ICD / `libvulkan_wrapper.so`, with GameNative owning
  LSFG layer installation and environment setup.

Do not infer one product's health from the other product's build result.
