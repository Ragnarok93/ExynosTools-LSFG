#include "layer_vk_struct_clone.h"

#include "layer_vk_struct_utils.h"

namespace {

bool graphics_pipeline_uses_color_attachment(const VkGraphicsPipelineCreateInfo& create_info) {
    if (!create_info.pColorBlendState) {
        return false;
    }

#ifdef VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO
    const auto* rendering_info = find_struct_in_pnext_chain<VkPipelineRenderingCreateInfo>(
        create_info.pNext,
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    if (rendering_info) {
        return rendering_info->colorAttachmentCount != 0;
    }
#endif

    return (create_info.renderPass != VK_NULL_HANDLE) ||
           (create_info.pColorBlendState->attachmentCount != 0);
}

bool graphics_pipeline_uses_depthstencil_attachment(const VkGraphicsPipelineCreateInfo& create_info) {
    if (!create_info.pDepthStencilState) {
        return false;
    }

#ifdef VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO
    const auto* rendering_info = find_struct_in_pnext_chain<VkPipelineRenderingCreateInfo>(
        create_info.pNext,
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    if (rendering_info) {
        return rendering_info->depthAttachmentFormat != VK_FORMAT_UNDEFINED ||
               rendering_info->stencilAttachmentFormat != VK_FORMAT_UNDEFINED;
    }
#endif

    return create_info.renderPass != VK_NULL_HANDLE;
}

} // namespace

vku::safe_VkGraphicsPipelineCreateInfo clone_graphics_pipeline_create_info(
    const VkGraphicsPipelineCreateInfo* create_info) {
    if (!create_info) {
        return vku::safe_VkGraphicsPipelineCreateInfo();
    }

    return vku::safe_VkGraphicsPipelineCreateInfo(
        create_info,
        graphics_pipeline_uses_color_attachment(*create_info),
        graphics_pipeline_uses_depthstencil_attachment(*create_info));
}

ClonedGraphicsPipelineCreateInfos clone_graphics_pipeline_create_infos(
    const VkGraphicsPipelineCreateInfo* create_infos,
    uint32_t create_info_count) {
    ClonedGraphicsPipelineCreateInfos cloned{};
    if (!create_infos || create_info_count == 0) {
        return cloned;
    }

    cloned.safe_infos.reserve(create_info_count);
    cloned.infos.reserve(create_info_count);
    for (uint32_t i = 0; i < create_info_count; ++i) {
        cloned.safe_infos.push_back(clone_graphics_pipeline_create_info(&create_infos[i]));
        cloned.infos.push_back(*cloned.safe_infos.back().ptr());
    }
    return cloned;
}

VkImageCopy* clone_copy_image_regions_to_legacy(
    const VkCopyImageInfo2& copy_info,
    TempArena<>& arena) {
    if (!copy_info.pRegions || copy_info.regionCount == 0) {
        return nullptr;
    }

    VkImageCopy* regions = arena.allocate_array<VkImageCopy>(copy_info.regionCount);
    for (uint32_t i = 0; i < copy_info.regionCount; ++i) {
        const VkImageCopy2& src_region = copy_info.pRegions[i];
        VkImageCopy& dst_region = regions[i];
        dst_region = {};
        dst_region.srcSubresource = src_region.srcSubresource;
        dst_region.srcOffset = src_region.srcOffset;
        dst_region.dstSubresource = src_region.dstSubresource;
        dst_region.dstOffset = src_region.dstOffset;
        dst_region.extent = src_region.extent;
    }

    return regions;
}

VkBufferImageCopy* clone_copy_buffer_to_image_regions_to_legacy(
    const VkCopyBufferToImageInfo2& copy_info,
    TempArena<>& arena) {
    if (!copy_info.pRegions || copy_info.regionCount == 0) {
        return nullptr;
    }

    VkBufferImageCopy* regions = arena.allocate_array<VkBufferImageCopy>(copy_info.regionCount);
    for (uint32_t i = 0; i < copy_info.regionCount; ++i) {
        const VkBufferImageCopy2& src_region = copy_info.pRegions[i];
        VkBufferImageCopy& dst_region = regions[i];
        dst_region = {};
        dst_region.bufferOffset = src_region.bufferOffset;
        dst_region.bufferRowLength = src_region.bufferRowLength;
        dst_region.bufferImageHeight = src_region.bufferImageHeight;
        dst_region.imageSubresource = src_region.imageSubresource;
        dst_region.imageOffset = src_region.imageOffset;
        dst_region.imageExtent = src_region.imageExtent;
    }

    return regions;
}

VkImageBlit* clone_blit_image_regions_to_legacy(
    const VkBlitImageInfo2& blit_info,
    TempArena<>& arena) {
    if (!blit_info.pRegions || blit_info.regionCount == 0) {
        return nullptr;
    }

    VkImageBlit* regions = arena.allocate_array<VkImageBlit>(blit_info.regionCount);
    for (uint32_t i = 0; i < blit_info.regionCount; ++i) {
        const VkImageBlit2& src_region = blit_info.pRegions[i];
        VkImageBlit& dst_region = regions[i];
        dst_region = {};
        dst_region.srcSubresource = src_region.srcSubresource;
        dst_region.srcOffsets[0] = src_region.srcOffsets[0];
        dst_region.srcOffsets[1] = src_region.srcOffsets[1];
        dst_region.dstSubresource = src_region.dstSubresource;
        dst_region.dstOffsets[0] = src_region.dstOffsets[0];
        dst_region.dstOffsets[1] = src_region.dstOffsets[1];
    }

    return regions;
}
