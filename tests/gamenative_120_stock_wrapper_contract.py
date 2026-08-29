#!/usr/bin/env python3
"""Validate ExynosTools against unmodified stock GameNative 1.2.0 wrappers.

Usage:
  python tests/gamenative_120_stock_wrapper_contract.py \
      <gamenative-v1.2.0-root> [driver.zip]
"""
from __future__ import annotations

import json
import subprocess
import sys
import zipfile
from pathlib import Path

EXPECTED_GN_SHA = "3491226faedb7222a5f8b7248c0247957a060836"
EXPECTED_DRIVER_LIBRARY = "libvulkan_exynostools.so"
EXPECTED_LAYER_LIBRARY = "libVkLayer_VortekXclipse.so"


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")
    print(f"PASS: {label}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        raise AssertionError(f"{label}: forbidden {needle!r}")
    print(f"PASS: {label}")


def read(root: Path, rel: str) -> str:
    path = root / rel
    if not path.is_file():
        raise AssertionError(f"missing source file: {rel}")
    return path.read_text(errors="replace")


def validate_repo(repo: Path) -> None:
    meta = json.loads((repo / "meta.json").read_text())
    assert meta["libraryName"] == EXPECTED_DRIVER_LIBRARY, meta
    assert meta["layerLibrary"] == EXPECTED_LAYER_LIBRARY, meta
    assert meta["layerName"] == "VK_LAYER_VORTEK_XCLIPSE", meta
    assert meta["name"].endswith("r7"), meta
    print("PASS: ExynosTools GameNative driver metadata")

    shim = read(repo, "src/driver/gamenative_wrapper_shim.cpp")
    shim_cmake = read(repo, "src/driver/CMakeLists.txt")
    dispatch_key = read(repo, "src/layer/layer_dispatch_key.h")
    require(shim, '"ADRENOTOOLS_DRIVER_PATH"', "stock Wrapper driver-directory input")
    require(shim, '"libVkLayer_VortekXclipse.so"', "ExynosTools compatibility layer host")

    # Physical-device r3 logs proved GameNative/AdrenoTools substitutes the
    # selected custom-driver .so at Android libvulkan's *vendor HAL* load site.
    require(shim, "HAL_MODULE_INFO_SYM", "Android Vulkan HAL HMI export")
    require(shim, "HWVULKAN_HARDWARE_MODULE_ID", "Android Vulkan HAL module id")
    require(shim, "HWVULKAN_DEVICE_0", "Android Vulkan HAL device id")
    require(shim, "hwvulkan_device_t", "Android hwvulkan device ABI")
    require(shim, "hal_OpenDevice", "HAL open callback")
    require(shim, "hal_EnumerateInstanceExtensionProperties", "HAL instance extension callback")
    require(shim, "hal_CreateInstance", "HAL instance creation callback")
    require(shim, "hal_GetInstanceProcAddr", "HAL instance proc callback")

    # r4 got through vkCreateInstance but Android libvulkan then overwrote the
    # first dispatch pointer before vkEnumeratePhysicalDevices. r5 uses stable
    # handle values only in the manually hosted below-loader mode.
    require(shim, 'setenv("EXYNOSTOOLS_DRIVER_HOSTED", "1", 1)', "HAL enables driver-hosted dispatch mode")
    require(dispatch_key, '"EXYNOSTOOLS_DRIVER_HOSTED"', "dispatch-key hosted-mode marker")
    require(dispatch_key, "driver_hosted_dispatch_keys", "hosted dispatch-key selector")
    require(dispatch_key, "stable_handle_key", "stable raw-handle key path")
    require(dispatch_key, "normal_layer_dispatch_key", "normal layer dispatch-pointer path")
    require(shim, "HAL vkEnumeratePhysicalDevices", "physical-device enumeration diagnostic")

    # The HAL must bypass the already-hooked Android libvulkan and open the real
    # Samsung HAL directly through the SP-HAL namespace.
    require(shim, '"libvndksupport.so"', "system SP-HAL support library")
    require(shim, "android_load_sphal_library", "direct SP-HAL loader API")
    require(shim, '"vulkan.samsung.so"', "Samsung Vulkan HAL target")
    forbid(shim, "adrenotools_open_libvulkan", "no nested AdrenoTools loader recursion")
    forbid(shim, 'dlopen("/system/lib64/libvulkan.so"', "no recursive raw system-loader open")
    forbid(shim, 'dlopen("/system/lib/libvulkan.so"', "no recursive raw 32-bit system-loader open")

    # Synthetic loader-chain records are private to the manually hosted layer.
    # Both instance and device create calls need a final adapter that removes the
    # synthetic head before entering Samsung's real hwvulkan implementation.
    require(shim, "VkLayerInstanceCreateInfo", "manual layer instance link")
    require(shim, "VkLayerDeviceCreateInfo", "manual layer device link")
    require(shim, "strip_synthetic_instance_link", "instance loader-link sanitization")
    require(shim, "strip_synthetic_device_link", "device loader-link sanitization")
    require(shim, "real_CreateInstance", "sanitized Samsung HAL instance create")
    require(shim, "real_CreateDevice", "sanitized Samsung HAL device create")
    require(shim, "Samsung HAL vkCreateDevice", "Samsung device-create diagnostic")
    require(shim, "real_next_gipa", "Samsung HAL next GIPA")
    require(shim, "real_next_gdpa", "Samsung HAL next GDPA")
    require(shim, "layer_link.pfnNextGetInstanceProcAddr = real_next_gipa", "layer chains to Samsung GIPA")
    require(shim, "layer_link.pfnNextGetDeviceProcAddr = real_next_gdpa", "layer chains to Samsung GDPA")

    # r6 was physically validated: the DXVK device request succeeds after the
    # layer's virtual BC capability is consumed only at Samsung's native boundary.
    require(shim, "vku::safe_VkDeviceCreateInfo", "pNext-safe device-create clone")
    require(shim_cmake, "Vulkan-Utility-Libraries", "shim safe-struct dependency")
    require(shim, "query_samsung_core_features", "native Samsung core-feature query")
    require(shim, "query_layer_core_features", "ExynosTools advertised core-feature query")
    require(shim, "consume_virtual_bc_device_feature", "virtual BC device-feature consumer")
    require(shim, "advertised_features.textureCompressionBC == VK_TRUE", "BC virtualization advertised-state gate")
    require(shim, "samsung_features.textureCompressionBC == VK_FALSE", "BC native-unsupported gate")
    require(shim, "features->textureCompressionBC = VK_FALSE", "legacy BC request consumption")
    require(shim, "features2->features.textureCompressionBC = VK_FALSE", "features2 BC request consumption")
    require(shim, "Consumed virtual textureCompressionBC device feature before Samsung vkCreateDevice", "virtual BC consumption diagnostic")
    require(shim, "Samsung HAL still reports VK_ERROR_FEATURE_NOT_PRESENT", "post-consumption failure diagnostic")
    require(shim, "Samsung missing requested core feature", "remaining core-feature diagnostic")
    require(shim, "Samsung rejected device feature pNext", "remaining pNext-feature diagnostic")

    # r7 observes the next physical milestone without taking ownership from
    # stock GameNative/LSFG. The GDPA pointer is logged and returned unchanged.
    require(shim, "is_passive_present_boundary_proc", "passive present-boundary selector")
    require(shim, "Passive present-boundary GDPA resolve", "passive present-boundary diagnostic")
    require(shim, "pointer returned unchanged", "unchanged present pointer contract")
    require(shim, '"vkCreateSwapchainKHR"', "swapchain-create resolution probe")
    require(shim, '"vkDestroySwapchainKHR"', "swapchain-destroy resolution probe")
    require(shim, '"vkGetSwapchainImagesKHR"', "swapchain-image resolution probe")
    require(shim, '"vkAcquireNextImageKHR"', "acquire-next-image resolution probe")
    require(shim, '"vkAcquireNextImage2KHR"', "acquire-next-image2 resolution probe")
    require(shim, '"vkQueuePresentKHR"', "queue-present resolution probe")
    require(shim, 'source=%s resolved=%d (pointer returned unchanged)', "passive GDPA-only behavior marker")
    forbid(shim, "shim_CreateSwapchainKHR", "no shim swapchain creation ownership")
    forbid(shim, "shim_DestroySwapchainKHR", "no shim swapchain destruction ownership")
    forbid(shim, "shim_AcquireNextImageKHR", "no shim acquire ownership")
    forbid(shim, "shim_AcquireNextImage2KHR", "no shim acquire2 ownership")
    forbid(shim, "shim_QueuePresentKHR", "no shim presentation ownership")

    require(shim, '"ExynosToolsShim"', "runtime diagnostic log tag")
    require(shim, "Samsung Vulkan HAL opened", "real HAL open diagnostic")
    require(shim, "HAL vkCreateInstance", "HAL instance diagnostic")
    require(shim, "HAL vkCreateDevice", "HAL device diagnostic")
    require(shim, "r7 initializing Android Vulkan HAL shim", "r7 runtime marker")

    assert not (repo / "ci/patch_gamenative_wrapper_lsfg.py").exists(), (
        "GameNative wrapper patcher must not exist"
    )
    assert not (repo / "tests/gamenative_120_wcp_contract.py").exists(), (
        "replacement Wrapper WCP contract must not exist"
    )
    print("PASS: no GameNative wrapper replacement path")


def validate_gamenative(root: Path) -> None:
    sha = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    assert sha == EXPECTED_GN_SHA, f"wrong GameNative revision: {sha}"
    print("PASS: exact stock GameNative 1.2.0 revision")

    adreno = read(root, "app/src/main/java/com/winlator/contents/AdrenotoolsManager.java")
    xserver = read(root, "app/src/main/java/app/gamenative/ui/screen/xserver/XServerScreen.kt")
    launcher = read(
        root,
        "app/src/main/java/com/winlator/xenvironment/components/BionicProgramLauncherComponent.java",
    )
    lsfg = read(root, "app/src/main/java/app/gamenative/utils/LsfgVkManager.kt")
    manifest = read(root, "app/src/main/assets/graphics_driver_download.json")

    require(adreno, 'jsonObject.getString("libraryName")', "stock custom-driver libraryName contract")
    require(adreno, 'envVars.put("ADRENOTOOLS_DRIVER_PATH"', "stock Wrapper driver path")
    require(adreno, 'envVars.put("ADRENOTOOLS_HOOKS_PATH"', "stock Wrapper hooks path")
    require(adreno, 'envVars.put("ADRENOTOOLS_DRIVER_NAME"', "stock Wrapper driver name")
    require(adreno, "setDriverById", "stock custom-driver selection")

    require(xserver, 'graphicsDriverConfig.get("version", DefaultVersion.WRAPPER)', "stock wrapper version selection")
    require(xserver, "adrenotoolsManager.setDriverById", "stock wrapper invokes AdrenoTools custom driver")
    require(xserver, 'startsWith("wrapper")', "stock wrapper component selection")
    require(manifest, '"id": "wrapper-gamenative"', "stock GameNative wrapper component")
    require(manifest, '"id": "wrapper-v2"', "stock alternate wrapper component")
    require(manifest, '"id": "wrapper-legacy"', "stock legacy wrapper component")

    require(launcher, "envVars.putAll(this.envVars)", "stock environment propagation")
    require(launcher, "LsfgVkManager.applyLaunchEnv", "stock LSFG launch integration")
    require(lsfg, 'ENV_CONFIG = "LSFG_CONFIG"', "stock LSFG config marker")
    require(lsfg, 'ENV_PROCESS = "LSFG_PROCESS"', "stock LSFG process marker")
    require(lsfg, 'RUNTIME_VERSION = "v1.3.3-android-arm64-v8a"', "stock bundled LSFG runtime")
    require(lsfg, "vkCreateSwapchainKHR", "stock LSFG owns swapchain interception")
    require(lsfg, "vkQueuePresentKHR", "stock LSFG owns presentation interception")
    require(lsfg, 'STATS_RELATIVE_PATH = ".config/lsfg-vk/stats.txt"', "stock LSFG measured-fps path")

    assert launcher.index("envVars.putAll(this.envVars)") < launcher.index(
        "LsfgVkManager.applyLaunchEnv"
    )
    print("PASS: stock environment order preserved")


def validate_zip(path: Path) -> None:
    assert path.is_file(), f"missing driver ZIP: {path}"
    with zipfile.ZipFile(path) as zf:
        names = zf.namelist()
        assert all("/" not in name.rstrip("/") for name in names), (
            f"driver ZIP must be root-flat for GameNative importer: {names}"
        )
        required = {"meta.json", EXPECTED_DRIVER_LIBRARY, EXPECTED_LAYER_LIBRARY}
        assert required.issubset(names), f"driver ZIP missing files: {required - set(names)}"
        assert "profile.json" not in names, "driver ZIP must not be a Wrapper WCP"
        assert "libvulkan_wrapper.so" not in names, "driver ZIP must not replace stock Wrapper"
        meta = json.loads(zf.read("meta.json"))
        assert meta["libraryName"] == EXPECTED_DRIVER_LIBRARY
        assert meta["name"].endswith("r7")
        for name in required:
            assert zf.getinfo(name).file_size > 0, f"empty driver member: {name}"
    print("PASS: GameNative custom-driver ZIP contract")


def main() -> None:
    if len(sys.argv) not in (2, 3):
        raise SystemExit(
            "usage: gamenative_120_stock_wrapper_contract.py <gamenative-root> [driver.zip]"
        )

    repo = Path(__file__).resolve().parents[1]
    validate_repo(repo)
    validate_gamenative(Path(sys.argv[1]).resolve())
    if len(sys.argv) == 3:
        validate_zip(Path(sys.argv[2]).resolve())

    print("PASS: stock GameNative 1.2.0 Wrapper + ExynosTools driver contract")


if __name__ == "__main__":
    main()
