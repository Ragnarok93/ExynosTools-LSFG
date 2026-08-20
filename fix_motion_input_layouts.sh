#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

FILE="vk_motion_execute.c"

cp "$FILE" "${FILE}.before-input-layout-fix"

python3 - "$FILE" <<'PY'
from pathlib import Path
import sys

p = Path(sys.argv[1])
s = p.read_text()

old_a = '''    barrierA.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierA.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrierA.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierA.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;'''

new_a = '''    barrierA.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierA.newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierA.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierA.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;'''

old_b = '''    barrierB.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrierB.newLayout =
        VK_IMAGE_LAYOUT_GENERAL;

    barrierB.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;

    barrierB.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;'''

new_b = '''    barrierB.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrierB.newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barrierB.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;

    barrierB.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;'''

if old_a not in s:
    raise SystemExit("ERROR: A input barrier block not found")

if old_b not in s:
    raise SystemExit("ERROR: B input barrier block not found")

s = s.replace(old_a, new_a, 1)
s = s.replace(old_b, new_b, 1)

p.write_text(s)
print("Input image layouts corrected.")
PY

echo
echo "=== VERIFY A ==="
sed -n '625,645p' "$FILE"

echo
echo "=== VERIFY B ==="
sed -n '750,775p' "$FILE"

echo
echo "=== COMPILE ==="

clang \
    -O2 \
    -Wall \
    -Wextra \
    -std=c11 \
    "$FILE" \
    -ldl \
    -o vk_motion_execute

echo
echo "COMPILE: PASS"
