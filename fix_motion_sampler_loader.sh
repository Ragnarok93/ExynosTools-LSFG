#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

FILE="vk_motion_execute.c"

if [[ ! -f "$FILE" ]]; then
    echo "ERROR: $FILE not found"
    exit 1
fi

cp "$FILE" "${FILE}.before-sampler-loader-fix"
echo "Backup: ${FILE}.before-sampler-loader-fix"

python3 - "$FILE" <<'PY'
from pathlib import Path
import sys

p = Path(sys.argv[1])
s = p.read_text()

# 1. Add vkCreateSampler to the device-function loading list.
needle = "    LOAD_DEVICE(vkCreateImageView);\n"

if needle not in s:
    raise SystemExit("ERROR: Could not find vkCreateImageView loader")

if "LOAD_DEVICE(vkCreateSampler);" not in s:
    s = s.replace(
        needle,
        needle + "    LOAD_DEVICE(vkCreateSampler);\n",
        1
    )

# 2. Add vkCreateSampler to the required-function check.
needle = "        !vkCreateImageView ||\n"

if needle not in s:
    raise SystemExit("ERROR: Could not find vkCreateImageView required check")

if "!vkCreateSampler" not in s:
    s = s.replace(
        needle,
        needle + "        !vkCreateSampler ||\n",
        1
    )

p.write_text(s)
print("vkCreateSampler loader added.")
PY

echo
echo "=== VERIFY LOADER ==="
grep -n -A3 -B3 'vkCreateSampler' "$FILE"

echo
echo "=== CHECK FOR DIRECT CALL ==="
grep -n 'vkCreateSampler(' "$FILE"

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
echo "=========================================="
echo "COMPILE: PASS"
echo "=========================================="
