#pragma once

#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/utility/vk_safe_struct.hpp>

#include "layer_temp_arena.h"

inline vku::safe_VkDeviceCreateInfo clone_device_create_info(const VkDeviceCreateInfo* create_info) {
    return create_info ? vku::safe_VkDeviceCreateInfo(create_info) : vku::safe_VkDeviceCreateInfo();
}

inline vku::safe_VkImageCreateInfo clone_image_create_info(const VkImageCreateInfo* create_info) {
    return create_info ? vku::safe_VkImageCreateInfo(create_info) : vku::safe_VkImageCreateInfo();
}

inline vku::safe_VkImageViewCreateInfo clone_image_view_create_info(const VkImageViewCreateInfo* create_info) {
    return create_info ? vku::safe_VkImageViewCreateInfo(create_info) : vku::safe_VkImageViewCreateInfo();
}

inline vku::safe_VkRenderPassCreateInfo clone_render_pass_create_info(const VkRenderPassCreateInfo* create_info) {
    return create_info ? vku::safe_VkRenderPassCreateInfo(create_info) : vku::safe_VkRenderPassCreateInfo();
}

inline vku::safe_VkFramebufferCreateInfo clone_framebuffer_create_info(
    const VkFramebufferCreateInfo* create_info) {
    return create_info ? vku::safe_VkFramebufferCreateInfo(create_info)
                       : vku::safe_VkFramebufferCreateInfo();
}

inline vku::safe_VkSamplerCreateInfo clone_sampler_create_info(const VkSamplerCreateInfo* create_info) {
    return create_info ? vku::safe_VkSamplerCreateInfo(create_info) : vku::safe_VkSamplerCreateInfo();
}

inline vku::safe_VkPipelineViewportStateCreateInfo clone_pipeline_viewport_state_create_info(
    const VkPipelineViewportStateCreateInfo* create_info,
    bool is_dynamic_viewports,
    bool is_dynamic_scissors) {
    return create_info ? vku::safe_VkPipelineViewportStateCreateInfo(
                             create_info,
                             is_dynamic_viewports,
                             is_dynamic_scissors)
                       : vku::safe_VkPipelineViewportStateCreateInfo();
}

inline vku::safe_VkPipelineDynamicStateCreateInfo clone_pipeline_dynamic_state_create_info(
    const VkPipelineDynamicStateCreateInfo* create_info) {
    return create_info ? vku::safe_VkPipelineDynamicStateCreateInfo(create_info)
                       : vku::safe_VkPipelineDynamicStateCreateInfo();
}

inline vku::safe_VkPipelineColorBlendStateCreateInfo clone_pipeline_color_blend_state_create_info(
    const VkPipelineColorBlendStateCreateInfo* create_info) {
    return create_info ? vku::safe_VkPipelineColorBlendStateCreateInfo(create_info)
                       : vku::safe_VkPipelineColorBlendStateCreateInfo();
}

#ifdef VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO
inline vku::safe_VkPipelineRenderingCreateInfo clone_pipeline_rendering_create_info(
    const VkPipelineRenderingCreateInfo* create_info) {
    return create_info ? vku::safe_VkPipelineRenderingCreateInfo(create_info)
                       : vku::safe_VkPipelineRenderingCreateInfo();
}
#endif

inline vku::safe_VkSpecializationInfo clone_specialization_info(const VkSpecializationInfo* specialization_info) {
    return specialization_info ? vku::safe_VkSpecializationInfo(specialization_info) : vku::safe_VkSpecializationInfo();
}

inline vku::safe_VkPipelineShaderStageCreateInfo clone_pipeline_shader_stage_create_info(
    const VkPipelineShaderStageCreateInfo* create_info) {
    return create_info ? vku::safe_VkPipelineShaderStageCreateInfo(create_info)
                       : vku::safe_VkPipelineShaderStageCreateInfo();
}

inline vku::safe_VkComputePipelineCreateInfo clone_compute_pipeline_create_info(
    const VkComputePipelineCreateInfo* create_info) {
    return create_info ? vku::safe_VkComputePipelineCreateInfo(create_info)
                       : vku::safe_VkComputePipelineCreateInfo();
}

struct ClonedGraphicsPipelineCreateInfos {
    std::vector<vku::safe_VkGraphicsPipelineCreateInfo> safe_infos;
    std::vector<VkGraphicsPipelineCreateInfo> infos;

    const VkGraphicsPipelineCreateInfo* data() const {
        return infos.empty() ? nullptr : infos.data();
    }
};

vku::safe_VkGraphicsPipelineCreateInfo clone_graphics_pipeline_create_info(
    const VkGraphicsPipelineCreateInfo* create_info);

ClonedGraphicsPipelineCreateInfos clone_graphics_pipeline_create_infos(
    const VkGraphicsPipelineCreateInfo* create_infos,
    uint32_t create_info_count);

template <typename CopyImageRegion>
VkImageCopy2* clone_copy_image_regions2(
    const CopyImageRegion* regions,
    uint32_t region_count,
    TempArena<>& arena) {
    if (!regions || region_count == 0) {
        return nullptr;
    }

    VkImageCopy2* cloned_regions = arena.allocate_array<VkImageCopy2>(region_count);
    for (uint32_t i = 0; i < region_count; ++i) {
        const CopyImageRegion& src_region = regions[i];
        VkImageCopy2& dst_region = cloned_regions[i];
        dst_region = {};
        dst_region.sType = static_cast<VkStructureType>(src_region.sType);
        dst_region.pNext = src_region.pNext;
        dst_region.srcSubresource = src_region.srcSubresource;
        dst_region.srcOffset = src_region.srcOffset;
        dst_region.dstSubresource = src_region.dstSubresource;
        dst_region.dstOffset = src_region.dstOffset;
        dst_region.extent = src_region.extent;
    }
    return cloned_regions;
}

template <typename CopyBufferImageRegion>
VkBufferImageCopy2* clone_copy_buffer_image_regions2(
    const CopyBufferImageRegion* regions,
    uint32_t region_count,
    TempArena<>& arena) {
    if (!regions || region_count == 0) {
        return nullptr;
    }

    VkBufferImageCopy2* cloned_regions = arena.allocate_array<VkBufferImageCopy2>(region_count);
    for (uint32_t i = 0; i < region_count; ++i) {
        const CopyBufferImageRegion& src_region = regions[i];
        VkBufferImageCopy2& dst_region = cloned_regions[i];
        dst_region = {};
        dst_region.sType = static_cast<VkStructureType>(src_region.sType);
        dst_region.pNext = src_region.pNext;
        dst_region.bufferOffset = src_region.bufferOffset;
        dst_region.bufferRowLength = src_region.bufferRowLength;
        dst_region.bufferImageHeight = src_region.bufferImageHeight;
        dst_region.imageSubresource = src_region.imageSubresource;
        dst_region.imageOffset = src_region.imageOffset;
        dst_region.imageExtent = src_region.imageExtent;
    }
    return cloned_regions;
}

template <typename BlitImageRegion>
VkImageBlit2* clone_blit_image_regions2(
    const BlitImageRegion* regions,
    uint32_t region_count,
    TempArena<>& arena) {
    if (!regions || region_count == 0) {
        return nullptr;
    }

    VkImageBlit2* cloned_regions = arena.allocate_array<VkImageBlit2>(region_count);
    for (uint32_t i = 0; i < region_count; ++i) {
        const BlitImageRegion& src_region = regions[i];
        VkImageBlit2& dst_region = cloned_regions[i];
        dst_region = {};
        dst_region.sType = static_cast<VkStructureType>(src_region.sType);
        dst_region.pNext = src_region.pNext;
        dst_region.srcSubresource = src_region.srcSubresource;
        dst_region.srcOffsets[0] = src_region.srcOffsets[0];
        dst_region.srcOffsets[1] = src_region.srcOffsets[1];
        dst_region.dstSubresource = src_region.dstSubresource;
        dst_region.dstOffsets[0] = src_region.dstOffsets[0];
        dst_region.dstOffsets[1] = src_region.dstOffsets[1];
    }
    return cloned_regions;
}

template <typename CopyImageInfo>
vku::safe_VkCopyImageInfo2 clone_copy_image_info2(
    const CopyImageInfo& copy_info,
    TempArena<>& arena) {
    auto* normalized = arena.allocate_array<VkCopyImageInfo2>(1);
    normalized[0] = {};
    normalized[0].sType = static_cast<VkStructureType>(copy_info.sType);
    normalized[0].pNext = copy_info.pNext;
    normalized[0].srcImage = copy_info.srcImage;
    normalized[0].srcImageLayout = copy_info.srcImageLayout;
    normalized[0].dstImage = copy_info.dstImage;
    normalized[0].dstImageLayout = copy_info.dstImageLayout;
    normalized[0].regionCount = copy_info.regionCount;
    normalized[0].pRegions =
        clone_copy_image_regions2(copy_info.pRegions, copy_info.regionCount, arena);
    return vku::safe_VkCopyImageInfo2(normalized);
}

template <typename CopyBufferToImageInfo>
vku::safe_VkCopyBufferToImageInfo2 clone_copy_buffer_to_image_info2(
    const CopyBufferToImageInfo& copy_info,
    TempArena<>& arena) {
    auto* normalized = arena.allocate_array<VkCopyBufferToImageInfo2>(1);
    normalized[0] = {};
    normalized[0].sType = static_cast<VkStructureType>(copy_info.sType);
    normalized[0].pNext = copy_info.pNext;
    normalized[0].srcBuffer = copy_info.srcBuffer;
    normalized[0].dstImage = copy_info.dstImage;
    normalized[0].dstImageLayout = copy_info.dstImageLayout;
    normalized[0].regionCount = copy_info.regionCount;
    normalized[0].pRegions = clone_copy_buffer_image_regions2(
        copy_info.pRegions,
        copy_info.regionCount,
        arena);
    return vku::safe_VkCopyBufferToImageInfo2(normalized);
}

template <typename BlitImageInfo>
vku::safe_VkBlitImageInfo2 clone_blit_image_info2(
    const BlitImageInfo& blit_info,
    TempArena<>& arena) {
    auto* normalized = arena.allocate_array<VkBlitImageInfo2>(1);
    normalized[0] = {};
    normalized[0].sType = static_cast<VkStructureType>(blit_info.sType);
    normalized[0].pNext = blit_info.pNext;
    normalized[0].srcImage = blit_info.srcImage;
    normalized[0].srcImageLayout = blit_info.srcImageLayout;
    normalized[0].dstImage = blit_info.dstImage;
    normalized[0].dstImageLayout = blit_info.dstImageLayout;
    normalized[0].regionCount = blit_info.regionCount;
    normalized[0].pRegions = clone_blit_image_regions2(
        blit_info.pRegions,
        blit_info.regionCount,
        arena);
    normalized[0].filter = blit_info.filter;
    return vku::safe_VkBlitImageInfo2(normalized);
}

VkImageCopy* clone_copy_image_regions_to_legacy(
    const VkCopyImageInfo2& copy_info,
    TempArena<>& arena);

VkBufferImageCopy* clone_copy_buffer_to_image_regions_to_legacy(
    const VkCopyBufferToImageInfo2& copy_info,
    TempArena<>& arena);

VkImageBlit* clone_blit_image_regions_to_legacy(
    const VkBlitImageInfo2& blit_info,
    TempArena<>& arena);
