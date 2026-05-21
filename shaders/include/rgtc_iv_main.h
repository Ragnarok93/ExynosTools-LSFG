#define VK_FORMAT_BC4_UNORM_BLOCK 139
#define VK_FORMAT_BC4_SNORM_BLOCK 140
#define VK_FORMAT_BC5_UNORM_BLOCK 141
#define VK_FORMAT_BC5_SNORM_BLOCK 142

layout(set = 0, binding = 1) readonly buffer uInputBlock {
    uint[] data;
} uInput;

layout(push_constant) uniform Registers
{
    int format;
    int width;
    int height;
    int offset;
    int bufferRowLength;
    int offsetX;
    int offsetY;
    int reserved0;
} registers;

void main()
{
    int format = registers.format;
    int width = registers.width;
    int height = registers.height;
    int offset = registers.offset;
    int bufferRowLength = registers.bufferRowLength;
    int offsetX = registers.offsetX;
    int offsetY = registers.offsetY;
    ivec2 resolution = ivec2(width, height);

    int x = int(gl_WorkGroupID.x * 8 + gl_LocalInvocationID.x);
    int y = int(gl_WorkGroupID.y * 8 + gl_LocalInvocationID.y);
    ivec2 coord = ivec2(x, y);

    bool is_snorm = (format == VK_FORMAT_BC4_SNORM_BLOCK || format == VK_FORMAT_BC5_SNORM_BLOCK);

    if (any(greaterThanEqual(coord, resolution)))
        return;

    ivec2 tile_coord = coord / 4;
    ivec2 pixel_coord = coord % 4;

    int rowExtent = max(bufferRowLength, width);
    int blocks_per_row = (rowExtent + 3) / 4;
    int base_word_offset = max(offset, 0) >> 2;
    int block_index = tile_coord.y * blocks_per_row + tile_coord.x;
    int bc_words = (format == VK_FORMAT_BC4_UNORM_BLOCK || format == VK_FORMAT_BC4_SNORM_BLOCK) ? 2 : 4;
    int block_offset = base_word_offset + bc_words * block_index;
    uvec4 payload = uvec4(uInput.data[block_offset],
                          uInput.data[block_offset + 1],
                          (bc_words > 2) ? uInput.data[block_offset + 2] : 0u,
                          (bc_words > 2) ? uInput.data[block_offset + 3] : 0u);

    int linear_pixel = 4 * pixel_coord.y + pixel_coord.x;

    vec4 rg = vec4(0);

    rg.x = is_snorm
        ? decode_alpha_rgtc_snorm(payload.xy, linear_pixel)
        : decode_alpha_rgtc(payload.xy, linear_pixel);

    if (format == VK_FORMAT_BC5_UNORM_BLOCK || format == VK_FORMAT_BC5_SNORM_BLOCK)
        rg.y = is_snorm
            ? decode_alpha_rgtc_snorm(payload.zw, linear_pixel)
            : decode_alpha_rgtc(payload.zw, linear_pixel);
    else
        rg.y = 0;

    rg.z = 0;
    rg.w = 1.0;

    ivec2 final_dst_pixel = ivec2(offsetX, offsetY) + coord;
    RGTC_STORE_PIXEL(final_dst_pixel, rg);
}
