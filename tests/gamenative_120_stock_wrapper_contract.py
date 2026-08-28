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
    print("PASS: ExynosTools GameNative driver metadata")

    shim = read(repo, "src/driver/gamenative_wrapper_shim.cpp")
    require(shim, '"ADRENOTOOLS_DRIVER_PATH"', "stock Wrapper driver-directory input")
    require(shim, '"/system/lib64/libvulkan.so"', "system Vulkan downstream")
    require(shim, '"libVkLayer_VortekXclipse.so"', "ExynosTools compatibility layer host")
    require(shim, "VkLayerInstanceCreateInfo", "manual layer instance link")
    require(shim, "VkLayerDeviceCreateInfo", "manual layer device link")
    require(shim, "g_runtime.layer_create_instance", "layer-owned instance creation")
    require(shim, "g_runtime.layer_create_device", "layer-owned device creation")

    # The manually hosted layer needs loader-link structs, but Android's real
    # libvulkan must never receive those private structs. The downstream
    # adapters strip only our synthetic head node while preserving the original
    # GameNative/LSFG pNext chain.
    require(shim, "strip_synthetic_instance_link", "instance loader-link sanitization")
    require(shim, "strip_synthetic_device_link", "device loader-link sanitization")
    require(shim, "downstream_CreateInstance", "sanitized downstream instance create")
    require(shim, "downstream_CreateDevice", "sanitized downstream device create")
    require(shim, "layer_link.pfnNextGetInstanceProcAddr = downstream_gipa", "layer uses sanitized next GIPA")
    require(shim, "layer_link.pfnNextGetDeviceProcAddr = downstream_gdpa", "layer uses sanitized next GDPA")
    forbid(shim, "layer_link.pfnNextGetInstanceProcAddr = g_runtime.system_gipa", "no raw system GIPA in synthetic link")
    forbid(shim, "layer_link.pfnNextGetDeviceProcAddr = g_runtime.system_gdpa", "no raw system GDPA in synthetic link")

    require(shim, "vk_icdGetInstanceProcAddr", "ICD instance proc compatibility export")
    require(shim, "vk_icdGetPhysicalDeviceProcAddr", "ICD physical-device proc compatibility export")
    require(shim, "vk_icdNegotiateLoaderICDInterfaceVersion", "ICD loader negotiation export")
    require(shim, '"ExynosToolsShim"', "runtime diagnostic log tag")

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
