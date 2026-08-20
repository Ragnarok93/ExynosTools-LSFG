from pathlib import Path
import re
import shutil

ROOT = Path.home() / "ExynosTools-LSFG"
SRC = ROOT / "vk_motion_execute.c"
DST = ROOT / "vk_motion_oracle_execute.c"

if not SRC.exists():
    raise SystemExit(f"ERROR: source not found: {SRC}")

shutil.copy2(SRC, DST)
s = DST.read_text()

# ------------------------------------------------------------
# 1. Oracle shader
# ------------------------------------------------------------

if '"lsfg_motion.spv"' not in s:
    raise SystemExit("ERROR: original SPIR-V filename not found")

s = s.replace(
    '"lsfg_motion.spv"',
    '"lsfg_motion_oracle.spv"',
    1
)

# ------------------------------------------------------------
# 2. Storage-capable images
# ------------------------------------------------------------

if "VK_IMAGE_USAGE_STORAGE_BIT" not in s:
    marker = "VK_IMAGE_USAGE_SAMPLED_BIT |"
    pos = s.find(marker)

    if pos < 0:
        raise SystemExit(
            "ERROR: VK_IMAGE_USAGE_SAMPLED_BIT usage block not found"
        )

    s = (
        s[:pos]
        + "VK_IMAGE_USAGE_SAMPLED_BIT |\n"
          "            VK_IMAGE_USAGE_STORAGE_BIT |"
        + s[pos + len(marker):]
    )

# ------------------------------------------------------------
# 3. Storage-image layouts
# ------------------------------------------------------------

s = s.replace(
    "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL",
    "VK_IMAGE_LAYOUT_GENERAL"
)

# ------------------------------------------------------------
# 4. Existing buffer supports UBO usage.
# ------------------------------------------------------------

if "VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT" not in s:
    marker = "VK_BUFFER_USAGE_TRANSFER_SRC_BIT |"
    pos = s.find(marker)

    if pos < 0:
        raise SystemExit(
            "ERROR: existing buffer usage declaration not found"
        )

    s = (
        s[:pos]
        + "VK_BUFFER_USAGE_TRANSFER_SRC_BIT |\n"
          "            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |"
        + s[pos + len(marker):]
    )

# ------------------------------------------------------------
# 5. Descriptor bindings.
#
# 0 = readonly storage image A
# 1 = readonly storage image B
# 2 = writeonly storage image output
# 3 = uniform buffer
# ------------------------------------------------------------

start = s.find("VkDescriptorSetLayoutBinding bindings[")

if start < 0:
    raise SystemExit(
        "ERROR: descriptor binding declaration not found"
    )

end = s.find(
    "VkDescriptorSetLayout descriptorSetLayout",
    start
)

if end < 0:
    raise SystemExit(
        "ERROR: descriptor set layout creation marker not found"
    )

layout = """VkDescriptorSetLayoutBinding bindings[4];

    memset(bindings, 0, sizeof(bindings));

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    """

s = s[:start] + layout + s[end:]

# ------------------------------------------------------------
# 6. Descriptor-set bindingCount = 4.
# ------------------------------------------------------------

layout_start = s.find(
    "VkDescriptorSetLayout descriptorSetLayout"
)

layout_end = s.find(
    "VkDescriptorPool descriptorPool",
    layout_start
)

if layout_start < 0 or layout_end < 0:
    raise SystemExit(
        "ERROR: descriptor layout region could not be located"
    )

layout_region = s[layout_start:layout_end]

layout_region2 = re.sub(
    r"\.bindingCount\s*=\s*\d+",
    ".bindingCount = 4",
    layout_region,
    count=1
)

if layout_region2 == layout_region:
    raise SystemExit(
        "ERROR: descriptor bindingCount could not be changed"
    )

s = (
    s[:layout_start]
    + layout_region2
    + s[layout_end:]
)

# ------------------------------------------------------------
# 7. Replace descriptor-pool create call.
# ------------------------------------------------------------

pool_start = s.find(
    'CHECK("vkCreateDescriptorPool"'
)

if pool_start < 0:
    raise SystemExit(
        "ERROR: vkCreateDescriptorPool CHECK block not found"
    )

pool_end = s.find(
    "VkDescriptorSet descriptorSet",
    pool_start
)

if pool_end < 0:
    raise SystemExit(
        "ERROR: descriptor-set allocation marker not found"
    )

pool_block = """CHECK("vkCreateDescriptorPool",
          vkCreateDescriptorPool(
              device,
              &(VkDescriptorPoolCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                  .maxSets = 1,
                  .poolSizeCount = 2,
                  .pPoolSizes =
                      (VkDescriptorPoolSize[]){
                          {
                              .type =
                                  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                              .descriptorCount = 3
                          },
                          {
                              .type =
                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              .descriptorCount = 1
                          }
                      }
              },
              NULL,
              &descriptorPool));

    """

s = s[:pool_start] + pool_block + s[pool_end:]

# ------------------------------------------------------------
# 8. Image descriptor infos.
# ------------------------------------------------------------

img_start = s.find(
    "VkDescriptorImageInfo imageInfos[3]"
)

if img_start < 0:
    raise SystemExit(
        "ERROR: imageInfos declaration not found"
    )

writes_start = s.find(
    "VkWriteDescriptorSet writes[",
    img_start
)

if writes_start < 0:
    raise SystemExit(
        "ERROR: descriptor writes declaration not found"
    )

img_block = """VkDescriptorImageInfo imageInfos[3];

    memset(imageInfos, 0, sizeof(imageInfos));

    for (int i = 0; i < 3; i++) {
        imageInfos[i].imageView = views[i];
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    """

s = s[:img_start] + img_block + s[writes_start:]

# ------------------------------------------------------------
# 9. Convert image descriptor writes to STORAGE_IMAGE.
# ------------------------------------------------------------

s = s.replace(
    "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER",
    "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE"
)

s = re.sub(
    r"\n\s*imageInfos\[\d+\]\.sampler\s*=\s*sampler\s*;",
    "",
    s
)

s = re.sub(
    r"\n\s*\.sampler\s*=\s*sampler\s*,",
    "",
    s
)

# ------------------------------------------------------------
# 10. Locate oracle descriptor-write section.
# ------------------------------------------------------------

writes_start = s.find(
    "VkWriteDescriptorSet writes["
)

if writes_start < 0:
    raise SystemExit(
        "ERROR: writes array disappeared"
    )

update_start = s.find(
    "vkUpdateDescriptorSets",
    writes_start
)

if update_start < 0:
    raise SystemExit(
        "ERROR: oracle vkUpdateDescriptorSets not found"
    )

writes_region = s[writes_start:update_start]

writes_region2 = re.sub(
    r"VkWriteDescriptorSet\s+writes\[\s*\d+\s*\]",
    "VkWriteDescriptorSet writes[4]",
    writes_region,
    count=1
)

if writes_region2 == writes_region:
    raise SystemExit(
        "ERROR: writes array size could not be changed"
    )

# ------------------------------------------------------------
# 11. Remap the existing host-visible buffer for UBO data.
#
# The original executor unmaps the staging buffer after frame B.
# Do NOT use the stale `mapped` pointer.
# ------------------------------------------------------------

ubo = """
    /*
     * The original upload path unmapped the staging buffer
     * after frame B. Remap it before writing oracle parameters.
     */
    uint8_t *oracleMapped = NULL;

    CHECK("vkMapMemory(oracle UBO)",
          vkMapMemory(
              device,
              bufferMemory,
              0,
              IMAGE_SIZE,
              0,
              (void **)&oracleMapped));

    struct OracleParams {
        float motion[2];
        float mode;
        float scale;
    };

    static const struct OracleParams oracleParams = {
        { -0.10f, 0.0f },
        0.0f,
        1.0f
    };

    /*
     * Keep the oracle UBO inside the existing buffer.
     * The image payload is already finished by this point.
     */
    size_t oracleParamOffset = IMAGE_SIZE - 16;

    memcpy(
        oracleMapped + oracleParamOffset,
        &oracleParams,
        sizeof(oracleParams));

    VkDescriptorBufferInfo oracleBufferInfo = {
        .buffer = buffer,
        .offset = oracleParamOffset,
        .range = 16
    };

    writes[3] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 3,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &oracleBufferInfo
    };

    vkUnmapMemory(device, bufferMemory);

    """

s = (
    s[:writes_start]
    + writes_region2
    + ubo
    + s[update_start:]
)

# ------------------------------------------------------------
# 12. Change ONLY the oracle vkUpdateDescriptorSets count.
# ------------------------------------------------------------

update_start = s.find(
    "vkUpdateDescriptorSets",
    writes_start
)

if update_start < 0:
    raise SystemExit(
        "ERROR: oracle vkUpdateDescriptorSets disappeared"
    )

update_tail = s[update_start:]

m = re.search(
    r"vkUpdateDescriptorSets\s*\(\s*device\s*,\s*(\d+)\s*,",
    update_tail
)

if not m:
    raise SystemExit(
        "ERROR: could not locate oracle descriptor-update count"
    )

a, b = m.span(1)

update_tail = (
    update_tail[:a]
    + "4"
    + update_tail[b:]
)

s = s[:update_start] + update_tail

# ------------------------------------------------------------
# 13. Replace OLD COLOR validation with ORACLE COORDINATE validation.
#
# The oracle shader writes:
#
#   R = normalized X coordinate sampled from frame A
#   G = normalized Y coordinate sampled from frame A
#   B = normalized X coordinate sampled from frame B
#   A = normalized Y coordinate sampled from frame B
#
# The shader uses:
#
#   delta = motion * scale
#   uv_a  = uv + delta
#   uv_b  = uv - delta
#
# With motion = (-0.10, 0), A moves left and B moves right.
# ------------------------------------------------------------

validation_start = s.find(
    "    /*\n     * Validate representative pixels.\n     */"
)

if validation_start < 0:
    raise SystemExit(
        "ERROR: old validation block marker not found"
    )

validation_end = s.find(
    "    vkUnmapMemory(device, bufferMemory);",
    validation_start
)

if validation_end < 0:
    raise SystemExit(
        "ERROR: validation block end marker not found"
    )

validation_end += len(
    "    vkUnmapMemory(device, bufferMemory);"
)

validation = r"""    /*
     * Validate the ORACLE SHADER output.
     *
     * The shader does NOT produce an interpolated RGBA color.
     *
     * Instead:
     *
     *   R = normalized sampled X coordinate from frame A
     *   G = normalized sampled Y coordinate from frame A
     *   B = normalized sampled X coordinate from frame B
     *   A = normalized sampled Y coordinate from frame B
     *
     * motion = (-0.10, 0.0)
     * scale  = 1.0
     *
     * Therefore:
     *
     *   uv_a = uv + (-0.10, 0)
     *   uv_b = uv - (-0.10, 0)
     *
     * and imageLoad uses integer coordinates obtained from:
     *
     *   floor(uv * imageSize)
     */

    unsigned tests[][2] = {
        {0, 0},
        {63, 0},
        {0, 63},
        {63, 63},
        {32, 32},
        {16, 48}
    };

    int failures = 0;

    const float motionX = -0.10f;
    const float motionY = 0.0f;
    const float scale = 1.0f;

    /*
     * Convert the floating-point oracle output to the same
     * UNORM8 representation used by an rgba8 image.
     */
    static uint8_t oracle_unorm8(float v)
    {
        if (v < 0.0f)
            v = 0.0f;

        if (v > 1.0f)
            v = 1.0f;

        float q = v * 255.0f;

        if (q <= 0.0f)
            return 0;

        if (q >= 255.0f)
            return 255;

        return (uint8_t)(q + 0.5f);
    }

    for (unsigned i = 0;
         i < sizeof(tests) / sizeof(tests[0]);
         i++) {

        unsigned x = tests[i][0];
        unsigned y = tests[i][1];

        size_t o =
            ((size_t)y * W + x) * 4;

        /*
         * Reproduce the shader exactly.
         *
         * uv = (p + 0.5) / size
         */
        float uvX =
            ((float)x + 0.5f) / (float)W;

        float uvY =
            ((float)y + 0.5f) / (float)H;

        float deltaX = motionX * scale;
        float deltaY = motionY * scale;

        /*
         * Shader:
         *
         *   uv_a = uv + delta
         *   uv_b = uv - delta
         */
        float uvAX = uvX + deltaX;
        float uvAY = uvY + deltaY;

        float uvBX = uvX - deltaX;
        float uvBY = uvY - deltaY;

        /*
         * Shader converts:
         *
         *   floor(uv * size)
         *
         * then clamps to [0, size - 1].
         */
        int paX = (int)floorf(uvAX * (float)W);
        int paY = (int)floorf(uvAY * (float)H);

        int pbX = (int)floorf(uvBX * (float)W);
        int pbY = (int)floorf(uvBY * (float)H);

        if (paX < 0)
            paX = 0;
        if (paX >= (int)W)
            paX = (int)W - 1;

        if (paY < 0)
            paY = 0;
        if (paY >= (int)H)
            paY = (int)H - 1;

        if (pbX < 0)
            pbX = 0;
        if (pbX >= (int)W)
            pbX = (int)W - 1;

        if (pbY < 0)
            pbY = 0;
        if (pbY >= (int)H)
            pbY = (int)H - 1;

        /*
         * Shader:
         *
         *   na = (pa + 0.5) / size
         *   nb = (pb + 0.5) / size
         */
        float naX =
            ((float)paX + 0.5f) / (float)W;

        float naY =
            ((float)paY + 0.5f) / (float)H;

        float nbX =
            ((float)pbX + 0.5f) / (float)W;

        float nbY =
            ((float)pbY + 0.5f) / (float)H;

        uint8_t expectedR = oracle_unorm8(naX);
        uint8_t expectedG = oracle_unorm8(naY);
        uint8_t expectedB = oracle_unorm8(nbX);
        uint8_t expectedA = oracle_unorm8(nbY);

        int ok =
            mapped[o + 0] == expectedR &&
            mapped[o + 1] == expectedG &&
            mapped[o + 2] == expectedB &&
            mapped[o + 3] == expectedA;

        printf(
            "pixel[%02u,%02u] "
            "Acoord=(%02d,%02d) "
            "Bcoord=(%02d,%02d) "
            "got=%02x %02x %02x %02x "
            "expected=%02x %02x %02x %02x %s\n",
            x,
            y,
            paX,
            paY,
            pbX,
            pbY,
            mapped[o + 0],
            mapped[o + 1],
            mapped[o + 2],
            mapped[o + 3],
            expectedR,
            expectedG,
            expectedB,
            expectedA,
            ok ? "OK" : "FAIL");

        if (!ok)
            failures++;
    }

    printf("\n=== TWO-FRAME ORACLE RESULT ===\n");

    if (failures == 0) {
        printf(
            "RESULT: PASS\n"
            "Xclipse 940 storage-image imageLoad/imageStore path "
            "matches oracle coordinate model.\n");
    } else {
        printf(
            "RESULT: FAIL\n"
            "Failures: %d\n",
            failures);
    }

    vkUnmapMemory(device, bufferMemory);"""

s = (
    s[:validation_start]
    + validation
    + s[validation_end:]
)

# ------------------------------------------------------------
# 14. Final sanity checks.
# ------------------------------------------------------------

checks = {
    "oracle shader":
        '"lsfg_motion_oracle.spv"',

    "four bindings":
        "VkDescriptorSetLayoutBinding bindings[4]",

    "four bindingCount":
        ".bindingCount = 4",

    "storage image":
        "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",

    "uniform buffer":
        "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER",

    "binding 3":
        ".dstBinding = 3",

    "oracle remap":
        'CHECK("vkMapMemory(oracle UBO)"',

    "oracle unmap":
        "vkUnmapMemory(device, bufferMemory);",

    "oracle coordinate validation":
        "TWO-FRAME ORACLE RESULT",

    "oracle update count":
        "vkUpdateDescriptorSets(\n        device,\n        4,",

    "storage usage":
        "VK_IMAGE_USAGE_STORAGE_BIT",

    "uniform buffer usage":
        "VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT",
}

for name, needle in checks.items():
    if needle not in s:
        raise SystemExit(
            f"ERROR: sanity check failed: {name}: {needle!r}"
        )

# We must not accidentally use the stale mapped pointer for the UBO.
if "mapped + oracleParamOffset" in s:
    raise SystemExit(
        "ERROR: stale mapped pointer still used for oracle UBO"
    )

# Make sure the descriptor pool contains both required types.
pool_region_start = s.find(
    'CHECK("vkCreateDescriptorPool"'
)

pool_region_end = s.find(
    "VkDescriptorSet descriptorSet",
    pool_region_start
)

if pool_region_start < 0 or pool_region_end < 0:
    raise SystemExit(
        "ERROR: final descriptor pool region not found"
    )

pool_region = s[pool_region_start:pool_region_end]

if "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE" not in pool_region:
    raise SystemExit(
        "ERROR: storage-image pool size missing"
    )

if "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER" not in pool_region:
    raise SystemExit(
        "ERROR: uniform-buffer pool size missing"
    )

if re.search(
    r"VkDescriptorPoolCreateInfo\)\s*\{\s*,",
    s,
    flags=re.S
):
    raise SystemExit(
        "ERROR: malformed descriptor-pool initializer detected"
    )

# There should be no old color-oracle expectation left.
if "expected=86 80 80 ff" in s:
    raise SystemExit(
        "ERROR: old interpolation-color validator still present"
    )

DST.write_text(s)

print(f"OK: generated {DST}")
