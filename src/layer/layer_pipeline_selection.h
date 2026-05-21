#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "vk_mem_alloc.h"

#include "layer_shared_types.h"

enum class DecoderShaderKind : uint32_t {
    None = 0,
    S3tc,
    RgtcR8Unorm,
    RgtcR8Snorm,
    RgtcRg8Unorm,
    RgtcRg8Snorm,
    RgtcRgba8Unorm,
    RgtcRgba8Snorm,
    Bc6,
    Bc7,
    CopyImageR8Unorm,
    CopyImageR8Snorm,
    CopyImageRg8Unorm,
    CopyImageRg8Snorm,
    CopyImageRgba8Unorm,
    CopyImageRgba8Snorm,
    CopyImageRgba16f,
    Count
};

enum class PipelineVariant : uint32_t {
    Default = 0,
    Wave32 = 1,
    Wave64 = 2,
    Count = 3
};

struct ComputeRuntime {
    std::mutex init_mutex;
    std::mutex descriptor_mutex;
    bool initialized = false;
    bool available = false;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptor_pools;
    std::vector<TrackedDescriptorSet> recycled_descriptor_sets;
    std::unordered_map<DecodeDescriptorSetKey, std::vector<TrackedDescriptorSet>, DecodeDescriptorSetKeyHash> cached_decode_descriptor_sets;
    std::unordered_map<SpecialCopyDescriptorSetKey, std::vector<TrackedDescriptorSet>, SpecialCopyDescriptorSetKeyHash> cached_special_copy_descriptor_sets;
    std::unordered_map<VkDescriptorSet, DescriptorSetCacheRecord> descriptor_set_cache_records;
    uint32_t descriptor_pool_capacity = 0;
    std::string pipeline_cache_path;
    bool pipeline_cache_dirty = false;
    bool use_descriptor_buffer = false;
    VkBuffer descriptor_buffer = VK_NULL_HANDLE;
    VmaAllocation descriptor_buffer_allocation = VK_NULL_HANDLE;
    void* descriptor_buffer_mapped = nullptr;
    VkDeviceAddress descriptor_buffer_address = 0;
    VkDeviceSize descriptor_buffer_size = 0;
    VkDeviceSize descriptor_buffer_next_offset = 0;
    VkDeviceSize descriptor_buffer_offset_alignment = 16;
    VkDeviceSize descriptor_buffer_layout_size = 0;
    VkDeviceSize descriptor_buffer_binding_offsets[3]{};
    size_t descriptor_buffer_binding_sizes[3]{};
    VkSampler copy_sampler = VK_NULL_HANDLE;
    std::array<std::array<VkPipeline, static_cast<size_t>(PipelineVariant::Count)>, static_cast<size_t>(DecoderShaderKind::Count)> pipelines{};
    uint32_t preferred_subgroup_size = 0;
    bool supports_wave32 = false;
    bool supports_wave64 = false;
    uint32_t descriptor_pool_growth_cap = 0;
};

DecoderShaderKind shader_kind_for_format(VkFormat format);
const char* shader_file_for_kind(DecoderShaderKind kind);
VkPipeline* pipeline_slot_for_kind(
    ComputeRuntime* runtime,
    DecoderShaderKind kind,
    PipelineVariant variant = PipelineVariant::Default);
VkPipeline pipeline_for_kind(
    const ComputeRuntime& runtime,
    DecoderShaderKind kind,
    PipelineVariant variant = PipelineVariant::Default);
PipelineVariant preferred_pipeline_variant(const ComputeRuntime& runtime);
VkPipeline choose_pipeline_for_kind(const ComputeRuntime& runtime, DecoderShaderKind kind);
DecoderShaderKind copy_image_shader_kind_for_format(VkFormat dst_format);
DecoderShaderKind shader_kind_for_decode(VkFormat requested_format, VkFormat real_format);
VkPipeline choose_decoder_pipeline(
    const ComputeRuntime& runtime,
    VkFormat requested_format,
    VkFormat real_format);
VkPipeline choose_copy_image_pipeline(const ComputeRuntime& runtime, VkFormat dst_actual_format);
