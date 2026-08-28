#!/usr/bin/env python3
import argparse
from pathlib import Path


def require(text, token, label):
    if token not in text:
        raise SystemExit(f"FAIL: {label}: missing {token!r}")
    print(f"PASS: {label}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--lsfg-root")
    ap.add_argument("--manager")
    args = ap.parse_args()
    root = Path(args.repo)

    entry = (root / "src/layer/layer_entry.cpp").read_text(errors="replace")
    compat = (root / "src/layer/layer_lsfg_compat.cpp").read_text(errors="replace")
    manifest = (root / "VkLayer_vortek_xclipse.json").read_text(errors="replace")

    require(compat, '"LSFG_PROCESS"', "GameNative process marker")
    require(compat, '"LSFG_CONFIG"', "GameNative config marker")
    require(entry, "!lsfg_process_active", "descriptor-buffer injection disabled for LSFG")
    require(entry, "lsfg_process_active || runtime.app.is_lsfg_framegen", "v1.3.3 shared-device prewarm gate")
    require(entry, "lsfg_compat.enabled && external_memory_image", "external BCn images bypass virtualization")
    require(manifest, '"name": "VK_LAYER_VORTEK_XCLIPSE"', "Vortek layer manifest")

    if args.lsfg_root:
        lsfg = Path(args.lsfg_root)
        hooks = (lsfg / "src/hooks.cpp").read_text(errors="replace")
        context = (lsfg / "src/context.cpp").read_text(errors="replace")
        require(context, "initializeExternal", "upstream single-device initialization")
        require(context, "createContextFromImages", "upstream shared VkImage context")
        require(context, "createDeviceLocal", "upstream device-local LSFG working images")
        require(hooks, "shaderStorageImageExtendedFormats", "upstream storage extended-format feature")
        require(hooks, "shaderStorageImageReadWithoutFormat", "upstream storage read-without-format feature")
        require(hooks, "shaderStorageImageWriteWithoutFormat", "upstream storage write-without-format feature")
        require(hooks, "shaderFloat16", "upstream FP16 feature merge")
        require(hooks, "vulkanMemoryModel", "upstream Vulkan memory-model merge")

    if args.manager:
        manager = Path(args.manager).read_text(errors="replace")
        require(manager, 'ENV_CONFIG = "LSFG_CONFIG"', "GameNative manager config environment")
        require(manager, 'ENV_PROCESS = "LSFG_PROCESS"', "GameNative manager process environment")
        require(manager, 'RUNTIME_VERSION = "v1.3.3-android-arm64-v8a"', "GameNative runtime version")

    print("PASS: LSFG compatibility contract")


if __name__ == "__main__":
    main()
