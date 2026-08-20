#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

FILE="vk_motion_execute.c"

if [[ ! -f "$FILE" ]]; then
    echo "ERROR: $FILE not found"
    exit 1
fi

cp "$FILE" "${FILE}.before-descriptor-fix"
echo "Backup: ${FILE}.before-descriptor-fix"

python3 - "$FILE" <<'PY'
from pathlib import Path
import sys

p = Path(sys.argv[1])
s = p.read_text()

old = '''    for (int i = 0; i < 3; i++) {
        bindings[i].binding = (uint32_t)i;
        bindings[i].descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags =
            VK_SHADER_STAGE_COMPUTE_BIT;
    }'''

new = '''    /*
     * Descriptor types must exactly match the SPIR-V:
     *
     * binding 0 = sampler2D frameA
     * binding 1 = sampler2D frameB
     * binding 2 = storage image outputImage
     */
    bindings[0].binding = 0;
    bindings[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;'''

if old not in s:
    raise SystemExit(
        "ERROR: Expected descriptor-binding block was not found"
    )

s = s.replace(old, new, 1)

p.write_text(s)
print("Descriptor set layout corrected.")
PY

echo
echo "=== DESCRIPTOR LAYOUT ==="
sed -n '835,910p' "$FILE"

echo
echo "=== SEARCHING DESCRIPTOR POOL ==="
grep -n -A20 -B5 \
    'vkCreateDescriptorPool' \
    "$FILE"

echo
echo "=== SEARCHING DESCRIPTOR WRITES ==="
grep -n -A30 -B5 \
    'VkWriteDescriptorSet' \
    "$FILE" || true
