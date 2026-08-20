#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

FILE="vk_motion_execute.c"

if [[ ! -f "$FILE" ]]; then
    echo "ERROR: $FILE not found"
    exit 1
fi

cp "$FILE" "${FILE}.before-sampler-descriptor-fix"
echo "Backup: ${FILE}.before-sampler-descriptor-fix"

python3 - "$FILE" <<'PY'
from pathlib import Path
import re
import sys

p = Path(sys.argv[1])
s = p.read_text()

# ------------------------------------------------------------
# 1. Fix descriptor pool.
# ------------------------------------------------------------

old_pool = '''                  .poolSizeCount = 1,
                  .pPoolSizes =
                      &(VkDescriptorPoolSize){
                          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                          3
                      }'''

new_pool = '''                  .poolSizeCount = 2,
                  .pPoolSizes =
                      (VkDescriptorPoolSize[]){
                          {
                              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              2
                          },
                          {
                              VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                              1
                          }
                      }'''

if old_pool not in s:
    raise SystemExit(
        "ERROR: Expected descriptor pool block was not found"
    )

s = s.replace(old_pool, new_pool, 1)

# ------------------------------------------------------------
# 2. Find the existing image-info construction and replace it
#    with sampler-aware image infos.
# ------------------------------------------------------------

start_marker = '''    VkDescriptorImageInfo imageInfos[3];'''

start = s.find(start_marker)

if start == -1:
    raise SystemExit(
        "ERROR: VkDescriptorImageInfo imageInfos[3] not found"
    )

end_marker = '''    VkWriteDescriptorSet writes[3];'''

end = s.find(end_marker, start)

if end == -1:
    raise SystemExit(
        "ERROR: VkWriteDescriptorSet block not found"
    )

replacement = '''    /*
     * Combined image samplers for frame A and frame B.
     *
     * The motion shader declares:
     *
     *   binding 0 = sampler2D frameA
     *   binding 1 = sampler2D frameB
     *   binding 2 = storage image outputImage
     */

    VkSampler sampler = VK_NULL_HANDLE;

    CHECK("vkCreateSampler",
          vkCreateSampler(
              device,
              &(VkSamplerCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                  .magFilter = VK_FILTER_LINEAR,
                  .minFilter = VK_FILTER_LINEAR,
                  .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                  .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .mipLodBias = 0.0f,
                  .anisotropyEnable = VK_FALSE,
                  .maxAnisotropy = 1.0f,
                  .compareEnable = VK_FALSE,
                  .compareOp = VK_COMPARE_OP_ALWAYS,
                  .minLod = 0.0f,
                  .maxLod = 0.0f,
                  .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
                  .unnormalizedCoordinates = VK_FALSE
              },
              NULL,
              &sampler));

    VkDescriptorImageInfo imageInfos[3];

    memset(imageInfos, 0, sizeof(imageInfos));

    /*
     * frame A sampler2D
     */
    imageInfos[0].sampler = sampler;
    imageInfos[0].imageView = views[0];
    imageInfos[0].imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    /*
     * frame B sampler2D
     */
    imageInfos[1].sampler = sampler;
    imageInfos[1].imageView = views[1];
    imageInfos[1].imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    /*
     * output storage image
     */
    imageInfos[2].sampler = VK_NULL_HANDLE;
    imageInfos[2].imageView = views[2];
    imageInfos[2].imageLayout =
        VK_IMAGE_LAYOUT_GENERAL;

'''

s = s[:start] + replacement + s[end:]

# ------------------------------------------------------------
# 3. Replace the generic descriptor-write loop.
# ------------------------------------------------------------

old_writes = '''    VkWriteDescriptorSet writes[3];

    for (int i = 0; i < 3; i++) {
        writes[i] = (VkWriteDescriptorSet){
            .sType =
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = (uint32_t)i,
            .descriptorCount = 1,
            .descriptorType =
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &imageInfos[i]
        };
    }'''

new_writes = '''    VkWriteDescriptorSet writes[3];

    writes[0] = (VkWriteDescriptorSet){
        .sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfos[0]
    };

    writes[1] = (VkWriteDescriptorSet){
        .sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfos[1]
    };

    writes[2] = (VkWriteDescriptorSet){
        .sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfos[2]
    };'''

if old_writes not in s:
    raise SystemExit(
        "ERROR: Expected descriptor write block was not found"
    )

s = s.replace(old_writes, new_writes, 1)

p.write_text(s)

print("Descriptor pool corrected.")
print("Sampler created and attached to frame A/B.")
print("Descriptor writes corrected.")
PY

echo
echo "=== VERIFY DESCRIPTOR POOL ==="
sed -n '938,970p' "$FILE"

echo
echo "=== VERIFY SAMPLER/DESCRIPTORS ==="
sed -n '975,1065p' "$FILE"

echo
echo "=== VERIFY DESCRIPTOR TYPES ==="
grep -n -A8 -B3 \
    'descriptorType' \
    "$FILE" | tail -50

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
