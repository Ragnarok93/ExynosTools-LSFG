#!/usr/bin/env python3

from pathlib import Path
import re
import shutil

NDK_VERSION = "27.2.12479018"

project = Path(".").resolve()
build_file = project / "app" / "build.gradle.kts"

if not build_file.is_file():
    raise SystemExit(f"ERROR: {build_file} not found")

text = build_file.read_text(encoding="utf-8")

pattern = re.compile(
    r'^(\s*)ndkVersion\s*=\s*["\'][^"\']+["\']',
    re.MULTILINE,
)

match = pattern.search(text)

if match:
    indent = match.group(1)
    new_text = pattern.sub(
        f'{indent}ndkVersion = "{NDK_VERSION}"',
        text,
        count=1,
    )
else:
    android = re.search(r'\bandroid\s*\{', text)

    if not android:
        raise SystemExit("ERROR: Could not find android { } block")

    start = android.end()
    depth = 1
    i = start

    while i < len(text) and depth:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1

    if depth:
        raise SystemExit("ERROR: Could not find end of android { } block")

    block = text[start:i - 1]

    indent_match = re.search(r'\n([ \t]+)\S', block)
    indent = indent_match.group(1) if indent_match else "    "

    insertion = f'\n{indent}ndkVersion = "{NDK_VERSION}"\n'
    new_text = text[:start] + insertion + text[start:]

if new_text == text:
    print(f"ndkVersion is already {NDK_VERSION}")
else:
    backup = build_file.with_name(build_file.name + ".bak")
    shutil.copy2(build_file, backup)
    build_file.write_text(new_text, encoding="utf-8")

    print("========================================")
    print(" Android NDK configuration updated")
    print("========================================")
    print(f"File    : {build_file}")
    print(f"NDK     : {NDK_VERSION}")
    print(f"Backup  : {backup}")

print()
print("Current setting:")
for line in build_file.read_text(encoding="utf-8").splitlines():
    if "ndkVersion" in line:
        print(line)
