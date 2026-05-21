#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

struct BcnCpuDecodeRegion {
    VkFormat compressed_format = VK_FORMAT_UNDEFINED;
    VkFormat output_format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t buffer_row_length = 0;
    uint32_t buffer_image_height = 0;
    VkDeviceSize buffer_offset = 0;
};

bool bcn_cpu_decode_region(
    const uint8_t* src_base,
    size_t src_size,
    const BcnCpuDecodeRegion& region,
    std::vector<uint8_t>* out_pixels);

uint32_t bcn_cpu_output_texel_size(VkFormat output_format);
