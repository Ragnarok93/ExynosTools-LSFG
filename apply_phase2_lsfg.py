#!/usr/bin/env python3

from pathlib import Path
from datetime import datetime
import hashlib
import re
import shutil
import sys

ROOT = Path.home() / "ExynosTools-LSFG"
TARGET = ROOT / "src/layer/layer_entry.cpp"

print("=" * 40)
print(" ExynosTools-LSFG Phase 2 Patcher")
print("=" * 40)
print()
print(f"Repository: {ROOT}")
print(f"Target:     {TARGET}")
print()

if not ROOT.is_dir():
    print("ERROR: repository directory does not exist.")
    sys.exit(1)

if not TARGET.is_file():
    print("ERROR: layer_entry.cpp does not exist.")
    sys.exit(1)

original = TARGET.read_text(encoding="utf-8")
original_hash = hashlib.sha256(original.encode()).hexdigest()

print("=== SOURCE PRECHECK ===")
print(f"SHA256: {original_hash}")

required = [
    "prepare_virtual_bcn_image_format_query(",
    "bcn_replacement_image_create_flags(",
    "bcn_replacement_format(",
    "normalize_virtual_bcn_image_format_properties(",
    "sanitize_virtual_bcn_output_pnext(",
    "layer_GetPhysicalDeviceImageFormatProperties2(",
    "layer_GetPhysicalDeviceImageFormatProperties2KHR(",
]

missing = [x for x in required if x not in original]

if missing:
    print()
    print("ERROR: current layer_entry.cpp does not contain the expected")
    print("Phase 1 BCn virtualization implementation.")
    print()
    for item in missing:
        print(f"  missing: {item}")
    print()
    print("NO FILE WAS WRITTEN.")
    sys.exit(1)

print("[+] Current Phase 1 BCn implementation detected")

# ------------------------------------------------------------
# Locate the existing prepare_virtual_bcn_image_format_query()
# ------------------------------------------------------------

prepare_marker = "bool prepare_virtual_bcn_image_format_query("

prepare_start = original.find(prepare_marker)

if prepare_start < 0:
    print("ERROR: prepare_virtual_bcn_image_format_query() not found.")
    sys.exit(1)

# Find its opening brace.
prepare_brace = original.find("{", prepare_start)

if prepare_brace < 0:
    print("ERROR: malformed prepare_virtual_bcn_image_format_query().")
    sys.exit(1)

depth = 0
prepare_end = None

for i in range(prepare_brace, len(original)):
    c = original[i]

    if c == "{":
        depth += 1
    elif c == "}":
        depth -= 1
        if depth == 0:
            prepare_end = i + 1
            break

if prepare_end is None:
    print("ERROR: could not determine prepare_virtual_bcn_image_format_query() boundary.")
    sys.exit(1)

prepare_body = original[prepare_start:prepare_end]

print(f"[+] Located prepare_virtual_bcn_image_format_query() at byte offset {prepare_start}")

# ------------------------------------------------------------
# Verify the existing replacement decision.
# ------------------------------------------------------------

replacement_flags = re.search(
    r"VkImageCreateFlags\s+replacement_flags\s*=\s*"
    r"bcn_replacement_image_create_flags\s*\(",
    prepare_body,
)

replacement_format = re.search(
    r"VkFormat\s+replacement\s*=\s*"
    r"bcn_replacement_format\s*\(",
    prepare_body,
)

if not replacement_flags or not replacement_format:
    print()
    print("ERROR: expected BCn replacement decision is not inside")
    print("prepare_virtual_bcn_image_format_query().")
    print()
    print("This means the local source differs from the Phase 1")
    print("implementation this patcher was designed for.")
    print()
    print("NO FILE WAS WRITTEN.")
    sys.exit(1)

print("[+] Found bcn_replacement_image_create_flags()")
print("[+] Found bcn_replacement_format()")

# ------------------------------------------------------------
# Phase 2 objective
#
# We need to make the image-format query path LSFG-aware without
# replacing the already-correct BCn virtualization machinery.
#
# The first required behavior is that the BCn query decision is
# made before forwarding to the physical driver, and that the
# virtualized query is normalized/sanitized afterwards.
#
# The current Phase 1 implementation already does this.
#
# Therefore Phase 2 adds an explicit helper that identifies an
# LSFG-sensitive BCn query and preserves the virtualized path.
# ------------------------------------------------------------

helper_marker = "bool is_lsfg_sensitive_bcn_image_format_query("

if helper_marker in original:
    print("[=] LSFG-sensitive BCn helper already exists")
else:
    insert_after = "bool has_incompatible_external_image_request(const void* pNext) {"

    pos = original.find(insert_after)

    if pos < 0:
        print("ERROR: could not find BCn query helper insertion point.")
        sys.exit(1)

    brace = original.find("{", pos)

    if brace < 0:
        print("ERROR: malformed has_incompatible_external_image_request().")
        sys.exit(1)

    depth = 0
    end = None

    for i in range(brace, len(original)):
        if original[i] == "{":
            depth += 1
        elif original[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break

    if end is None:
        print("ERROR: could not determine helper boundary.")
        sys.exit(1)

    helper = r'''

bool is_lsfg_sensitive_bcn_image_format_query(
    const VkPhysicalDeviceImageFormatInfo2& info) {
    if (!is_bcn_format(info.format)) {
        return false;
    }

    // LSFG consumes images through the normal Vulkan image/view path.
    // Queries involving external-memory handles or unsupported image
    // view combinations cannot be safely represented by the internal
    // decoded backing image.
    if (has_incompatible_external_image_request(info.pNext)) {
        return true;
    }

    if (has_incompatible_image_view_format_request(info)) {
        return true;
    }

    return false;
}
'''

    original = original[:end] + helper + original[end:]
    print("[+] Added LSFG-sensitive BCn query helper")

# ------------------------------------------------------------
# Add LSFG-sensitive handling to both ImageFormatProperties2 paths.
# ------------------------------------------------------------

def patch_query_function(source, function_name):
    marker = f"VKAPI_ATTR VkResult VKAPI_CALL {function_name}("

    start = source.find(marker)

    if start < 0:
        raise RuntimeError(f"{function_name}() not found")

    brace = source.find("{", start)

    if brace < 0:
        raise RuntimeError(f"Malformed {function_name}()")

    depth = 0
    end = None

    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break

    if end is None:
        raise RuntimeError(f"Could not determine {function_name}() boundary")

    body = source[start:end]

    marker2 = """    const bool requested_external_image_query =
        has_incompatible_external_image_request(pImageFormatInfo->pNext);
"""

    if marker2 not in body:
        raise RuntimeError(
            f"{function_name}(): expected external-query decision was not found"
        )

    replacement = marker2 + """    const bool lsfg_sensitive_bcn_query =
        is_lsfg_sensitive_bcn_image_format_query(*pImageFormatInfo);
"""

    body = body.replace(marker2, replacement, 1)

    # Preserve the existing Phase 1 behavior, but make the intent explicit:
    # LSFG-sensitive BCn queries must not accidentally escape the
    # virtualization path.
    old = """    if (is_bcn_format(pImageFormatInfo->format) &&
        has_incompatible_image_view_format_request(*pImageFormatInfo)) {
        return fail_virtual_bcn_image_format_query(pImageFormatProperties);
    }
"""

    if old not in body:
        raise RuntimeError(
            f"{function_name}(): expected BCn view-format guard was not found"
        )

    new = """    if (lsfg_sensitive_bcn_query &&
        has_incompatible_image_view_format_request(*pImageFormatInfo)) {
        return fail_virtual_bcn_image_format_query(pImageFormatProperties);
    }
"""

    body = body.replace(old, new, 1)

    return source[:start] + body + source[end:]


try:
    original = patch_query_function(
        original,
        "layer_GetPhysicalDeviceImageFormatProperties2",
    )

    original = patch_query_function(
        original,
        "layer_GetPhysicalDeviceImageFormatProperties2KHR",
    )
except RuntimeError as exc:
    print()
    print(f"ERROR: {exc}")
    print()
    print("NO FILE WAS WRITTEN.")
    sys.exit(1)

print("[+] Patched core ImageFormatProperties2 path")
print("[+] Patched KHR ImageFormatProperties2 path")

# ------------------------------------------------------------
# Final source verification
# ------------------------------------------------------------

checks = [
    "bool is_lsfg_sensitive_bcn_image_format_query(",
    "const bool lsfg_sensitive_bcn_query =",
    "prepare_virtual_bcn_image_format_query(",
    "bcn_replacement_image_create_flags(",
    "bcn_replacement_format(",
    "normalize_virtual_bcn_image_format_properties(",
    "sanitize_virtual_bcn_output_pnext(",
]

for check in checks:
    if check not in original:
        print()
        print(f"ERROR: post-patch verification failed: {check}")
        print("NO FILE WAS WRITTEN.")
        sys.exit(1)

if original == TARGET.read_text(encoding="utf-8"):
    print()
    print("ERROR: patch produced no changes.")
    sys.exit(1)

# ------------------------------------------------------------
# Backup and write.
# ------------------------------------------------------------

timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = TARGET.with_name(
    f"{TARGET.name}.bak-phase2-{timestamp}"
)

shutil.copy2(TARGET, backup)
TARGET.write_text(original, encoding="utf-8")

new_hash = hashlib.sha256(original.encode()).hexdigest()

print()
print("=== PATCH COMPLETE ===")
print(f"[+] Backup: {backup}")
print(f"[+] New SHA256: {new_hash}")
print()
print("=== VERIFYING FILE ===")

written = TARGET.read_text(encoding="utf-8")

if hashlib.sha256(written.encode()).hexdigest() != new_hash:
    print("ERROR: written file hash mismatch.")
    sys.exit(1)

for check in checks:
    if check not in written:
        print(f"ERROR: missing post-write marker: {check}")
        sys.exit(1)

print("[+] File hash verified")
print("[+] All Phase 2 markers verified")
print()
print("Phase 2 patch applied successfully.")
print()
print("IMPORTANT:")
print("  Review the diff before building:")
print()
print("    git diff -- src/layer/layer_entry.cpp")
print()
