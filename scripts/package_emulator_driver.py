#!/usr/bin/env python3
"""Package the ExynosTools-LSFG Vulkan layer for non-GameNative emulators.

This script packages the Vulkan layer built from this repository.  A complete
importable driver bundle also needs the Samsung Vulkan backend used by the
emulator.  Supply that file with --backend; use --layer-only only for CI or for
launchers that can layer on top of an already-installed Samsung backend.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REQUIRED_EXPORTS = (
    "vkGetInstanceProcAddr",
    "vkGetDeviceProcAddr",
    "vkNegotiateLoaderLayerInterfaceVersion",
)


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def run(*args: str) -> str:
    result = subprocess.run(args, check=False, text=True, capture_output=True)
    if result.returncode != 0:
        fail(f"command failed ({' '.join(args)}):\n{result.stdout}{result.stderr}")
    return result.stdout + result.stderr


def validate_layer(layer: Path) -> None:
    if not layer.is_file() or layer.stat().st_size == 0:
        fail(f"missing layer ELF: {layer}")

    header = run("readelf", "-h", str(layer))
    if "AArch64" not in header:
        fail("layer is not an AArch64 ELF")

    symbols = run("readelf", "-Ws", str(layer))
    missing = [name for name in REQUIRED_EXPORTS if name not in symbols]
    if missing:
        fail("layer is missing Vulkan loader exports: " + ", ".join(missing))

    dynamic = run("readelf", "-d", str(layer))
    if "RUNPATH" in dynamic or "RPATH" in dynamic:
        fail("layer contains an RPATH/RUNPATH and is not portable")


def validate_metadata(repo: Path) -> tuple[dict, dict]:
    meta = json.loads((repo / "meta.json").read_text())
    manifest = json.loads((repo / "VkLayer_vortek_xclipse.json").read_text())
    layer = manifest.get("layer", {})

    if meta.get("layerLibrary") != "libVkLayer_VortekXclipse.so":
        fail("meta.json layerLibrary does not match the built layer name")
    if meta.get("layerName") != "VK_LAYER_VORTEK_XCLIPSE":
        fail("meta.json layerName is not VK_LAYER_VORTEK_XCLIPSE")
    if layer.get("library_path") != meta.get("layerLibrary"):
        fail("manifest library_path and meta.json layerLibrary disagree")
    if layer.get("name") != meta.get("layerName"):
        fail("manifest layer name and meta.json layerName disagree")
    return meta, manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=Path("build-android"))
    parser.add_argument("--backend", type=Path, help="Samsung Vulkan backend to package as vulkan.samsung.so")
    parser.add_argument("--layer-only", action="store_true", help="Create a CI/test layer bundle without a backend")
    parser.add_argument("--output", type=Path, default=Path("dist/ExynosTools-LSFG-General-Emulator.zip"))
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = args.build_dir if args.build_dir.is_absolute() else repo / args.build_dir
    layer = build_dir / "libVkLayer_VortekXclipse.so"

    if bool(args.backend) == bool(args.layer_only):
        fail("choose exactly one of --backend <vulkan.samsung.so> or --layer-only")

    validate_layer(layer)
    meta, _ = validate_metadata(repo)

    backend = args.backend.resolve() if args.backend else None
    if backend and (not backend.is_file() or backend.stat().st_size == 0):
        fail(f"missing backend: {backend}")

    output = args.output if args.output.is_absolute() else repo / args.output
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="etlsfg-package-") as tmp:
        stage = Path(tmp)
        shutil.copy2(layer, stage / "libVkLayer_VortekXclipse.so")
        shutil.copy2(repo / "VkLayer_vortek_xclipse.json", stage / "VkLayer_vortek_xclipse.json")
        shutil.copy2(repo / "meta.json", stage / "meta.json")
        if backend:
            shutil.copy2(backend, stage / "vulkan.samsung.so")

        mode = "complete" if backend else "layer-only"
        install = (
            "ExynosTools-LSFG general emulator package\n"
            f"mode: {mode}\n\n"
            "This is the non-GameNative Vulkan-layer product built from this repository.\n"
            "It is intentionally separate from the GameNative Mesa Wrapper WCP.\n\n"
        )
        if backend:
            install += (
                "The archive contains the Samsung backend, layer manifest, metadata, and\n"
                "libVkLayer_VortekXclipse.so and is suitable for launchers that implement\n"
                "the upstream ExynosTools/Vortek layerLibrary + layerName package model.\n"
            )
        else:
            install += (
                "This CI artifact intentionally omits vulkan.samsung.so. It is not a\n"
                "standalone custom-driver package. Combine it with the emulator/device's\n"
                "matching Samsung Vulkan backend, or re-run package_emulator_driver.py\n"
                "with --backend.\n"
            )
        (stage / "INSTALL.txt").write_text(install)

        names = sorted(p.name for p in stage.iterdir())
        expected = {"INSTALL.txt", "VkLayer_vortek_xclipse.json", "libVkLayer_VortekXclipse.so", "meta.json"}
        if backend:
            expected.add("vulkan.samsung.so")
        if set(names) != expected:
            fail(f"unexpected package contents: {names}")

        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
            for path in sorted(stage.iterdir()):
                zf.write(path, path.name)

    print(f"PACKAGE={output}")
    print(f"MODE={'complete' if backend else 'layer-only'}")
    print(f"DRIVER={meta.get('libraryName')}")


if __name__ == "__main__":
    main()
