#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path

EXPECTED_GAMENATIVE_120_SHA = "3491226faedb7222a5f8b7248c0247957a060836"


def require(text, token, label):
    if token not in text:
        raise SystemExit(f"FAIL: {label}: missing {token!r}")
    print(f"PASS: {label}")


def verify_manager(manager_text):
    require(manager_text, 'ENV_CONFIG = "LSFG_CONFIG"', "GameNative manager config environment")
    require(manager_text, 'ENV_PROCESS = "LSFG_PROCESS"', "GameNative manager process environment")
    require(
        manager_text,
        'RUNTIME_VERSION = "v1.3.3-android-arm64-v8a"',
        "GameNative 1.2.0 bundled LSFG runtime",
    )
    require(manager_text, "VK_LAYER_PATH", "GameNative implicit-layer path")
    require(manager_text, "VkLayer_LS_frame_generation.json", "GameNative LSFG manifest")
    require(manager_text, "liblsfg-vk-layer.so", "GameNative LSFG layer library")


def verify_stock_gamenative_120(gamenative_root):
    root = Path(gamenative_root)
    try:
        sha = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"FAIL: unable to identify GameNative revision: {exc}") from exc

    if sha != EXPECTED_GAMENATIVE_120_SHA:
        raise SystemExit(
            "FAIL: wrong GameNative revision: "
            f"expected {EXPECTED_GAMENATIVE_120_SHA}, got {sha}"
        )
    print("PASS: stock GameNative v1.2.0 revision")

    manager_path = root / "app/src/main/java/app/gamenative/utils/LsfgVkManager.kt"
    launcher_path = root / (
        "app/src/main/java/com/winlator/xenvironment/components/"
        "BionicProgramLauncherComponent.java"
    )
    if not manager_path.is_file():
        raise SystemExit(f"FAIL: missing stock GameNative LSFG manager: {manager_path}")
    if not launcher_path.is_file():
        raise SystemExit(f"FAIL: missing stock GameNative Bionic launcher: {launcher_path}")

    verify_manager(manager_path.read_text(errors="replace"))
    require(
        launcher_path.read_text(errors="replace"),
        "LsfgVkManager",
        "Bionic LSFG launch integration",
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--lsfg-root")
    ap.add_argument("--manager")
    ap.add_argument("--gamenative-root")
    args = ap.parse_args()
    root = Path(args.repo)

    entry = (root / "src/layer/layer_entry.cpp").read_text(errors="replace")
    compat = (root / "src/layer/layer_lsfg_compat.cpp").read_text(errors="replace")
    manifest = (root / "VkLayer_vortek_xclipse.json").read_text(errors="replace")

    require(compat, '"LSFG_PROCESS"', "GameNative process marker")
    require(compat, '"LSFG_CONFIG"', "GameNative config marker")
    require(entry, "!lsfg_process_active", "descriptor-buffer injection disabled for LSFG")
    require(
        entry,
        "lsfg_process_active || runtime.app.is_lsfg_framegen",
        "stock GameNative 1.2.0 bundled-LSFG shared-device prewarm gate",
    )
    require(
        entry,
        "lsfg_compat.enabled && external_memory_image",
        "external BCn images bypass virtualization",
    )
    require(manifest, '"name": "VK_LAYER_VORTEK_XCLIPSE"', "Vortek layer manifest")

    if args.lsfg_root:
        lsfg = Path(args.lsfg_root)
        hooks = (lsfg / "src/hooks.cpp").read_text(errors="replace")
        context = (lsfg / "src/context.cpp").read_text(errors="replace")
        require(context, "initializeExternal", "explicit upstream single-device initialization")
        require(context, "createContextFromImages", "explicit upstream shared VkImage context")
        require(context, "createDeviceLocal", "explicit upstream device-local LSFG working images")
        require(hooks, "shaderStorageImageExtendedFormats", "explicit upstream storage extended-format feature")
        require(hooks, "shaderStorageImageReadWithoutFormat", "explicit upstream storage read-without-format feature")
        require(hooks, "shaderStorageImageWriteWithoutFormat", "explicit upstream storage write-without-format feature")
        require(hooks, "shaderFloat16", "explicit upstream FP16 feature merge")
        require(hooks, "vulkanMemoryModel", "explicit upstream Vulkan memory-model merge")

    if args.manager:
        verify_manager(Path(args.manager).read_text(errors="replace"))

    if args.gamenative_root:
        verify_stock_gamenative_120(args.gamenative_root)

    print("PASS: LSFG compatibility contract")


if __name__ == "__main__":
    main()
