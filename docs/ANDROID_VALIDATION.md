# Android Vulkan Layer Validation

This guide validates the **general emulator/Vortek** product built from this
repository. It does not validate the separate GameNative Mesa Wrapper WCP.

## Build

Build the `VkLayer_VortekXclipse` target using the instructions in the root
README or the `Build General Emulator Vulkan Layer` GitHub Actions workflow.

Expected files:

```text
build-android/libVkLayer_VortekXclipse.so
VkLayer_vortek_xclipse.json
```

## Static ELF checks

```bash
SO=build-android/libVkLayer_VortekXclipse.so
readelf -h "$SO" | grep -E 'Class:|Type:|Machine:'
readelf -Ws "$SO" | grep -E \
  'vkGetInstanceProcAddr|vkGetDeviceProcAddr|vkNegotiateLoaderLayerInterfaceVersion'
if readelf -d "$SO" | grep -E 'RPATH|RUNPATH'; then
  echo 'FAIL: non-portable RPATH/RUNPATH'
  exit 1
fi
```

The target must be AArch64 and all three loader entry points must be globally
visible.

## Direct Android layer smoke test

Push the layer and manifest to an accessible test directory:

```bash
adb shell 'mkdir -p /data/local/tmp/exynostools-lsfg'
adb push build-android/libVkLayer_VortekXclipse.so /data/local/tmp/exynostools-lsfg/
adb push VkLayer_vortek_xclipse.json /data/local/tmp/exynostools-lsfg/
```

For a debuggable Vulkan target whose loader honors Android debug-layer
properties:

```bash
adb shell setprop debug.vulkan.layer_path /data/local/tmp/exynostools-lsfg
adb shell setprop debug.vulkan.layers VK_LAYER_VORTEK_XCLIPSE
```

Restart the target application, then inspect logs:

```bash
adb logcat -c
adb logcat | grep -Ei 'Vortek|ExynosTools|Vulkan|VUID'
```

The important first proof is that the loader discovers
`VK_LAYER_VORTEK_XCLIPSE` and successfully enters the layer. Only after that
should game-specific BCn or pipeline behavior be investigated.

Disable the test layer when finished:

```bash
adb shell setprop debug.vulkan.layers ''
adb shell setprop debug.vulkan.layer_path ''
```

## Emulator package validation

A standalone custom-driver package needs the matching Samsung backend as well as
the layer. Create it with:

```bash
python scripts/package_emulator_driver.py \
  --build-dir build-android \
  --backend /path/to/vulkan.samsung.so \
  --output dist/ExynosTools-LSFG-General-Emulator.zip
```

Do not test the GameNative `libvulkan_wrapper.so` WCP as though it were this
product; the loader contracts are different.
