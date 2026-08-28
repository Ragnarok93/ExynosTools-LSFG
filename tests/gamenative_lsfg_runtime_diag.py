#!/usr/bin/env python3
"""Validate one Wrapper diagnostic captured from GameNative 1.2.0 + LSFG."""
from __future__ import annotations

import re
import sys
from pathlib import Path


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL: {label}: missing {needle!r}")
    print(f"PASS: {label}")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: gamenative_lsfg_runtime_diag.py <wrapper_diag.txt>")

    path = Path(sys.argv[1])
    if not path.is_file():
        raise SystemExit(f"FAIL: missing diagnostic file: {path}")
    text = path.read_text(errors="replace")

    require(text, "WRAPPER DIAGNOSTICS", "Wrapper diagnostic header")
    require(text, "driver=Samsung (Xclipse)", "Samsung Xclipse Vulkan driver")
    require(text, "--- ExynosTools LSFG integration ---", "ExynosTools LSFG diagnostic block")
    require(text, "contract: GameNative-1.2.0@3491226f", "GameNative 1.2.0 wrapper contract")
    require(text, "active: yes", "LSFG environment active")
    require(text, "process: gamenative-lsfg", "stock GameNative LSFG process marker")
    require(text, "config: set", "stock GameNative LSFG config marker")
    require(text, "backend: system-vulkan", "system Vulkan backend")
    require(text, "incoming pNext: present", "incoming LSFG feature pNext chain")
    require(text, "NULL-pNext fallback: disabled", "unsafe NULL-pNext fallback disabled")
    require(text, "result: 0 (VK_SUCCESS)", "shared-device vkCreateDevice success")

    device = re.search(r"device:\s*([^\r\n]+)", text)
    api = re.search(r"apiVersion=([0-9]+\.[0-9]+\.[0-9]+)", text)
    if device:
        print(f"INFO: device={device.group(1).strip()}")
    if api:
        print(f"INFO: apiVersion={api.group(1)}")
    print("PASS: stock GameNative 1.2.0 LSFG runtime diagnostic")


if __name__ == "__main__":
    main()
