#!/usr/bin/env python3
"""Validate the ExynosTools wrapper WCP against unmodified GameNative v1.2.0.

Usage:
  python tests/gamenative_120_wcp_contract.py <gamenative-v1.2.0-root> <file.wcp>
"""
from __future__ import annotations

import json
import subprocess
import sys
import tarfile
from pathlib import Path

EXPECTED_GN_SHA = "3491226faedb7222a5f8b7248c0247957a060836"
EXPECTED_WRAPPER_NAME = "ExynosTools-LSFG"
EXPECTED_FILES = {
    "libvulkan_wrapper.so": "${libdir}/libvulkan_wrapper.so",
    "wrapper_icd.aarch64.json": "${sharedir}/vulkan/icd.d/wrapper_icd.aarch64.json",
    "libadrenotools.so": "${libdir}/libadrenotools.so",
}
EXPECTED_WRAPPER_MARKERS = (
    b"ExynosTools LSFG: preserving shared-device feature chain",
    b"ExynosTools LSFG: refusing NULL-pNext fallback",
    b"--- ExynosTools LSFG integration ---",
    b"contract: GameNative-1.2.0@3491226f",
    b"backend: %s",
    b"incoming pNext: %s",
    b"NULL-pNext fallback: %s",
)


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


def read(root: Path, rel: str) -> str:
    path = root / rel
    if not path.is_file():
        raise AssertionError(f"missing stock source file: {rel}")
    return path.read_text(errors="replace")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: gamenative_120_wcp_contract.py <gamenative-root> <wcp>")

    root = Path(sys.argv[1]).resolve()
    wcp = Path(sys.argv[2]).resolve()

    sha = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    assert sha == EXPECTED_GN_SHA, f"wrong GameNative revision: {sha}"

    profile_src = read(root, "app/src/main/java/com/winlator/contents/ContentProfile.java")
    manager_src = read(root, "app/src/main/java/com/winlator/contents/ContentsManager.java")
    lsfg_src = read(root, "app/src/main/java/app/gamenative/utils/LsfgVkManager.kt")
    env_tab_src = read(
        root,
        "app/src/main/java/app/gamenative/ui/component/dialog/EnvironmentTab.kt",
    )
    launcher_src = read(
        root,
        "app/src/main/java/com/winlator/xenvironment/components/BionicProgramLauncherComponent.java",
    )

    require(profile_src, 'CONTENT_TYPE_WRAPPER("Wrapper")', "Wrapper content type")
    for target in EXPECTED_FILES.values():
        require(manager_src, target, "Wrapper trusted target")
    require(manager_src, "WRAPPER_TRUST_FILES", "Wrapper trusted-file table")
    require(manager_src, "applyContent", "custom content application")

    source_text = "\n".join(
        p.read_text(errors="replace")
        for p in (root / "app/src/main/java").rglob("*")
        if p.suffix in {".kt", ".java"}
    )
    require(source_text, '"Wrapper-$it"', "custom Wrapper UI naming")
    require(source_text, "getProfileByEntryName", "custom Wrapper profile lookup")
    require(source_text, "applyContent(wrapperProfile)", "custom Wrapper activation")

    # Stock GameNative 1.2.0 exposes arbitrary per-container environment variables,
    # and the Bionic launcher merges them into the process environment. Therefore
    # WRAPPER_DIAG=1 can be enabled without modifying the app codebase.
    require(env_tab_src, "showEnvVarCreateDialog", "environment variable creation UI")
    require(env_tab_src, "envVars.put(envVarName, envVarValue)", "arbitrary environment variable storage")
    require(launcher_src, "envVars.putAll(this.envVars)", "custom environment propagation")

    # Stock LSFG manager must own the frame-generation runtime and implicit layer.
    # The WCP replaces only GameNative's one Wrapper component.
    require(lsfg_src, "v1.3.3-android-arm64-v8a", "stock LSFG runtime")
    require(lsfg_src, "LSFG_PROCESS", "stock LSFG process env")
    require(lsfg_src, "LSFG_CONFIG", "stock LSFG config env")
    require(lsfg_src, "VK_LAYER_PATH", "stock implicit-layer path")
    require(lsfg_src, "VkLayer_LS_frame_generation.json", "stock LSFG manifest")
    require(lsfg_src, "liblsfg-vk-layer.so", "stock LSFG layer library")
    require(launcher_src, "LsfgVkManager", "Bionic LSFG launch integration")

    assert wcp.is_file(), f"missing WCP: {wcp}"
    with tarfile.open(wcp, "r:xz") as tf:
        names = tf.getnames()
        assert names and names[0] == "profile.json", "profile.json must be first/root entry"
        assert all("/" not in n for n in names), f"WCP must be flat: {names}"
        expected_archive = {"profile.json", *EXPECTED_FILES.keys()}
        assert set(names) == expected_archive, f"unexpected WCP contents: {names}"

        profile_f = tf.extractfile("profile.json")
        assert profile_f is not None
        profile = json.loads(profile_f.read().decode())
        assert profile["type"] == "Wrapper"
        assert profile["versionName"] == EXPECTED_WRAPPER_NAME
        assert isinstance(profile["versionCode"], int) and profile["versionCode"] >= 1
        assert profile.get("description")
        got = {entry["source"]: entry["target"] for entry in profile["files"]}
        assert got == EXPECTED_FILES, f"profile file map mismatch: {got}"
        for source in EXPECTED_FILES:
            member = tf.getmember(source)
            assert member.isfile() and member.size > 0, f"bad WCP member: {source}"

        wrapper_f = tf.extractfile("libvulkan_wrapper.so")
        assert wrapper_f is not None
        wrapper_bytes = wrapper_f.read()
        for marker in EXPECTED_WRAPPER_MARKERS:
            assert marker in wrapper_bytes, f"shipping wrapper missing LSFG marker: {marker!r}"

    print("GameNative v1.2.0 custom Wrapper + LSFG WCP contract: PASS")


if __name__ == "__main__":
    main()
