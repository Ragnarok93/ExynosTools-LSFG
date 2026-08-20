# Phase 3C: Android-native vendor ICD probe

This probe isolates the Android linker-namespace problem discovered during Phase 3B.

The Termux test can load `libVkLayer_VortekXclipse.so`, but Termux's default linker namespace cannot load Samsung's `/vendor/lib64/hw/vulkan.samsung.so` or its vendor dependencies. The probe therefore runs as a normal Android application process and performs two Vulkan instance tests in the same process:

1. Baseline: Vulkan without Vortek.
2. Vortek: Vulkan with `VK_LAYER_VORTEK_XCLIPSE` enabled through an explicit layer manifest generated inside the app's private files directory.

The Vortek layer binary is staged into the APK's ARM64 JNI directory from the normal Android layer build output. The probe does **not** bundle or copy Samsung's vendor ICD or its dependencies.

## Build

From the repository root after building the Android ARM64 layer:

```sh
./tests/android_native_probe/stage_layer.sh
cd tests/android_native_probe
./gradlew :app:assembleDebug
```

The expected staged file is:

```text
tests/android_native_probe/app/src/main/jniLibs/arm64-v8a/libVkLayer_VortekXclipse.so
```

The binary is intentionally ignored by Git.

## Install and run

With an Android device connected through ADB:

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am force-stop com.exynostools.androidprobe
adb shell monkey -p com.exynostools.androidprobe 1
adb logcat -c
adb logcat -s ExynosTools-Phase3C:V
```

The app also displays a compact result summary on screen.

## Expected Phase 3C evidence

A successful baseline should identify the real Samsung/Xclipse Vulkan device rather than llvmpipe.

The Vortek run must then show all of the following:

```text
Vortek enumeration: FOUND
VORTEK vkCreateInstance: VK_SUCCESS
VORTEK GPU0: name=<Samsung Xclipse ...>
```

If baseline succeeds but Vortek fails, the logcat output is the primary diagnostic source. In particular, capture any loader messages mentioning:

- `vulkan.samsung.so`
- `libsbwchelper.so`
- linker namespaces
- `VK_LAYER_VORTEK_XCLIPSE`
- `dlopen`
- `vkCreateInstance`

A successful Phase 3C result proves that the existing Vortek layer can execute in an Android application process while the real Samsung vendor ICD remains the backend. It does **not** yet prove LSFG frame generation or BCn virtualization on Xclipse; those remain subsequent integration tests.
