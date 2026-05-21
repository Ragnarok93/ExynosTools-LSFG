#include "layer_pipeline_selection.h"

namespace {

size_t shader_kind_index(DecoderShaderKind kind) {
    return static_cast<size_t>(kind);
}

size_t pipeline_variant_index(PipelineVariant variant) {
    return static_cast<size_t>(variant);
}

bool is_valid_shader_kind(DecoderShaderKind kind) {
    return kind != DecoderShaderKind::None &&
           shader_kind_index(kind) < static_cast<size_t>(DecoderShaderKind::Count);
}

}  // namespace

DecoderShaderKind shader_kind_for_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return DecoderShaderKind::S3tc;
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
            return DecoderShaderKind::RgtcRgba8Unorm;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            return DecoderShaderKind::Bc6;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return DecoderShaderKind::Bc7;
        default:
            return DecoderShaderKind::None;
    }
}

const char* shader_file_for_kind(DecoderShaderKind kind) {
    switch (kind) {
        case DecoderShaderKind::S3tc:
            return "s3tc_iv.comp.spv";
        case DecoderShaderKind::RgtcR8Unorm:
            return "rgtc_iv_r8.comp.spv";
        case DecoderShaderKind::RgtcR8Snorm:
            return "rgtc_iv_r8_snorm.comp.spv";
        case DecoderShaderKind::RgtcRg8Unorm:
            return "rgtc_iv_rg8.comp.spv";
        case DecoderShaderKind::RgtcRg8Snorm:
            return "rgtc_iv_rg8_snorm.comp.spv";
        case DecoderShaderKind::RgtcRgba8Unorm:
            return "rgtc_iv_rgba8.comp.spv";
        case DecoderShaderKind::RgtcRgba8Snorm:
            return "rgtc_iv_rgba8_snorm.comp.spv";
        case DecoderShaderKind::Bc6:
            return "bc6_iv.comp.spv";
        case DecoderShaderKind::Bc7:
            return "bc7_iv.comp.spv";
        case DecoderShaderKind::CopyImageR8Unorm:
            return "copy_image_iv_r8.comp.spv";
        case DecoderShaderKind::CopyImageR8Snorm:
            return "copy_image_iv_r8_snorm.comp.spv";
        case DecoderShaderKind::CopyImageRg8Unorm:
            return "copy_image_iv_rg8.comp.spv";
        case DecoderShaderKind::CopyImageRg8Snorm:
            return "copy_image_iv_rg8_snorm.comp.spv";
        case DecoderShaderKind::CopyImageRgba8Unorm:
            return "copy_image_iv_rgba8.comp.spv";
        case DecoderShaderKind::CopyImageRgba8Snorm:
            return "copy_image_iv_rgba8_snorm.comp.spv";
        case DecoderShaderKind::CopyImageRgba16f:
            return "copy_image_iv_rgba16f.comp.spv";
        default:
            return nullptr;
    }
}

VkPipeline* pipeline_slot_for_kind(
    ComputeRuntime* runtime,
    DecoderShaderKind kind,
    PipelineVariant variant) {
    if (!runtime || !is_valid_shader_kind(kind)) {
        return nullptr;
    }
    return &runtime->pipelines[shader_kind_index(kind)][pipeline_variant_index(variant)];
}

VkPipeline pipeline_for_kind(
    const ComputeRuntime& runtime,
    DecoderShaderKind kind,
    PipelineVariant variant) {
    if (!is_valid_shader_kind(kind)) {
        return VK_NULL_HANDLE;
    }
    return runtime.pipelines[shader_kind_index(kind)][pipeline_variant_index(variant)];
}

PipelineVariant preferred_pipeline_variant(const ComputeRuntime& runtime) {
    if (runtime.preferred_subgroup_size == 32u && runtime.supports_wave32) {
        return PipelineVariant::Wave32;
    }
    if (runtime.preferred_subgroup_size == 64u && runtime.supports_wave64) {
        return PipelineVariant::Wave64;
    }
    if (runtime.supports_wave32) {
        return PipelineVariant::Wave32;
    }
    if (runtime.supports_wave64) {
        return PipelineVariant::Wave64;
    }
    return PipelineVariant::Default;
}

VkPipeline choose_pipeline_for_kind(const ComputeRuntime& runtime, DecoderShaderKind kind) {
    if (!is_valid_shader_kind(kind)) {
        return VK_NULL_HANDLE;
    }

    const PipelineVariant preferred_variant = preferred_pipeline_variant(runtime);
    VkPipeline pipeline = pipeline_for_kind(runtime, kind, preferred_variant);
    if (pipeline != VK_NULL_HANDLE) {
        return pipeline;
    }

    if (preferred_variant != PipelineVariant::Wave32) {
        pipeline = pipeline_for_kind(runtime, kind, PipelineVariant::Wave32);
        if (pipeline != VK_NULL_HANDLE) {
            return pipeline;
        }
    }
    if (preferred_variant != PipelineVariant::Wave64) {
        pipeline = pipeline_for_kind(runtime, kind, PipelineVariant::Wave64);
        if (pipeline != VK_NULL_HANDLE) {
            return pipeline;
        }
    }

    return pipeline_for_kind(runtime, kind, PipelineVariant::Default);
}

DecoderShaderKind copy_image_shader_kind_for_format(VkFormat dst_format) {
    switch (dst_format) {
        case VK_FORMAT_R8_UNORM:
            return DecoderShaderKind::CopyImageR8Unorm;
        case VK_FORMAT_R8_SNORM:
            return DecoderShaderKind::CopyImageR8Snorm;
        case VK_FORMAT_R8G8_UNORM:
            return DecoderShaderKind::CopyImageRg8Unorm;
        case VK_FORMAT_R8G8_SNORM:
            return DecoderShaderKind::CopyImageRg8Snorm;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return DecoderShaderKind::CopyImageRgba8Unorm;
        case VK_FORMAT_R8G8B8A8_SNORM:
            return DecoderShaderKind::CopyImageRgba8Snorm;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return DecoderShaderKind::CopyImageRgba16f;
        default:
            return DecoderShaderKind::None;
    }
}

DecoderShaderKind shader_kind_for_decode(VkFormat requested_format, VkFormat real_format) {
    switch (requested_format) {
        case VK_FORMAT_BC4_UNORM_BLOCK:
            switch (real_format) {
                case VK_FORMAT_R8_UNORM:
                    return DecoderShaderKind::RgtcR8Unorm;
                case VK_FORMAT_R8G8B8A8_UNORM:
                    return DecoderShaderKind::RgtcRgba8Unorm;
                default:
                    return DecoderShaderKind::None;
            }
        case VK_FORMAT_BC4_SNORM_BLOCK:
            switch (real_format) {
                case VK_FORMAT_R8_SNORM:
                    return DecoderShaderKind::RgtcR8Snorm;
                case VK_FORMAT_R8G8B8A8_SNORM:
                    return DecoderShaderKind::RgtcRgba8Snorm;
                default:
                    return DecoderShaderKind::None;
            }
        case VK_FORMAT_BC5_UNORM_BLOCK:
            switch (real_format) {
                case VK_FORMAT_R8G8_UNORM:
                    return DecoderShaderKind::RgtcRg8Unorm;
                case VK_FORMAT_R8G8B8A8_UNORM:
                    return DecoderShaderKind::RgtcRgba8Unorm;
                default:
                    return DecoderShaderKind::None;
            }
        case VK_FORMAT_BC5_SNORM_BLOCK:
            switch (real_format) {
                case VK_FORMAT_R8G8_SNORM:
                    return DecoderShaderKind::RgtcRg8Snorm;
                case VK_FORMAT_R8G8B8A8_SNORM:
                    return DecoderShaderKind::RgtcRgba8Snorm;
                default:
                    return DecoderShaderKind::None;
            }
        default:
            return shader_kind_for_format(requested_format);
    }
}

VkPipeline choose_decoder_pipeline(
    const ComputeRuntime& runtime,
    VkFormat requested_format,
    VkFormat real_format) {
    return choose_pipeline_for_kind(runtime, shader_kind_for_decode(requested_format, real_format));
}

VkPipeline choose_copy_image_pipeline(const ComputeRuntime& runtime, VkFormat dst_actual_format) {
    return choose_pipeline_for_kind(runtime, copy_image_shader_kind_for_format(dst_actual_format));
}
