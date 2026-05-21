layout(set = 0, binding = 2) uniform sampler2D uSrc;

layout(push_constant) uniform Registers
{
    int srcOffsetX;
    int srcOffsetY;
    int dstOffsetX;
    int dstOffsetY;
    int width;
    int height;
    int reserved0;
    int reserved1;
} registers;

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 extent = ivec2(registers.width, registers.height);
    if (any(greaterThanEqual(coord, extent)))
        return;

    ivec2 src_coord = ivec2(registers.srcOffsetX, registers.srcOffsetY) + coord;
    ivec2 dst_coord = ivec2(registers.dstOffsetX, registers.dstOffsetY) + coord;

    vec4 texel = texelFetch(uSrc, src_coord, 0);
    COPY_IMAGE_STORE(dst_coord, texel);
}
