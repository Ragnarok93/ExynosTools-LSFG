# Android Validation Guide (Step 4)

This guide validates the layer before game testing.

## 1) Build

```powershell
.\scripts\configure_android_local_repos.ps1
```

Artifacts:

- `build-android/libVkLayer_ExynosTools.so`
- `build-android/VkLayer_exynostools.json`

## 2) Push files to device

```powershell
adb push build-android/libVkLayer_ExynosTools.so /data/local/tmp/exynostools/
adb push build-android/VkLayer_exynostools.json /data/local/tmp/exynostools/
adb push exynostools_config.ini /data/local/tmp/exynostools/
```

## 3) Enable validation + ExynosTools layer

```powershell
adb shell setprop debug.vulkan.layer_path /data/local/tmp/exynostools
adb shell setprop debug.vulkan.layers VK_LAYER_KHRONOS_validation:VK_LAYER_EXYNOSTOOLS_bcn
```

Restart the target app after changing properties.

Optional: configure the layer either with `exynostools_config.ini` or
programmatically through `VK_EXT_layer_settings`.

## 4) Inspect logs

```powershell
adb logcat -c
adb logcat | findstr /I "ExynosToolsLayer VUID validation"
```

If you see `VUID-*` messages, fix them before large game tests.

## 5) Disable when done

```powershell
adb shell setprop debug.vulkan.layers ""
adb shell setprop debug.vulkan.layer_path ""
```
