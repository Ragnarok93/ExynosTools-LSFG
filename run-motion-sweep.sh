#!/data/data/com.termux/files/usr/bin/bash
set -eu

cd "$(dirname "$0")"

for motion in 0.10 0.0 -0.10; do
    echo
    echo "============================================================"
    echo "MOTION SWEEP: $motion"
    echo "============================================================"

    python3 - "$motion" <<'PY'
from pathlib import Path
import sys

motion = sys.argv[1]

# Shader
p = Path("lsfg_motion.comp")
s = p.read_text()

import re

s2, n = re.subn(
    r"vec2 motion = vec2\([^;]+\);",
    f"vec2 motion = vec2({motion}, 0.0);",
    s,
    count=1,
)

if n != 1:
    raise SystemExit("ERROR: shader motion declaration not found")

p.write_text(s2)

# Host oracle
p = Path("vk_motion_execute.c")
s = p.read_text()

s2, n = re.subn(
    r"const float motionX\s*=\s*[-+0-9.]+f\s*;",
    f"const float motionX = {motion}f;",
    s,
    count=1,
)

if n != 1:
    raise SystemExit("ERROR: host motion placeholder not found")

p.write_text(s2)

print(f"PATCHED MOTION={motion}")
PY

    rm -f lsfg_motion.spv

    glslc lsfg_motion.comp -o lsfg_motion.spv
    spirv-val lsfg_motion.spv

    clang -O2 -Wall -Wextra -std=c11 \
        vk_motion_execute.c \
        -ldl \
        -o vk_motion_execute

    echo
    grep -n 'vec2 motion' lsfg_motion.comp

    echo
    spirv-dis lsfg_motion.spv |
        grep -n -E 'float_n0_100000001|float_0_100000001|OpConstant.*%float'

    echo
    ./vk_motion_execute \
        2>&1 |
        tee "xclipse940-motion-${motion}.txt"

    status=${PIPESTATUS[0]}

    if [ "$status" -ne 0 ]; then
        echo
        echo "SWEEP RESULT: FAIL at motion=$motion"
        exit "$status"
    fi
done

echo
echo "============================================================"
echo "MOTION SWEEP: ALL THREE TESTS PASSED"
echo "============================================================"
