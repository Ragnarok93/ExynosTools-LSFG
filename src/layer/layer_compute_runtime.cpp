#include "layer_compute_runtime.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "exynostools_embedded_spirv.h"
#include "layer_descriptor_write_builder.h"
#include "layer_global_state.h"
#include "layer_logging.h"
#include "layer_vk_struct_clone.h"

namespace {

constexpr uint32_t kDescriptorPoolInitialMaxSetsFallback = 4096u;
constexpr VkDeviceSize kDescriptorBufferSizeBytes = 16ull * 1024ull * 1024ull;

bool read_spirv_file(const std::string& filename, std::vector<uint32_t>* out_words) {
    if (!out_words || filename.empty()) {
        return false;
    }
    const exynostools_embedded_spirv::ShaderBlob* blob =
        exynostools_embedded_spirv::find_shader_blob(filename.c_str());
    if (!blob || !blob->words || blob->word_count == 0) {
        return false;
    }
    out_words->assign(blob->words, blob->words + blob->word_count);
    return true;
}

bool read_binary_file(const std::string& filename, std::vector<uint8_t>* out_bytes) {
    if (!out_bytes || filename.empty()) {
        return false;
    }

    std::ifstream input(filename, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        return false;
    }

    std::streamsize size = input.tellg();
    if (size <= 0) {
        return false;
    }
    input.seekg(0, std::ios::beg);

    out_bytes->resize(static_cast<size_t>(size));
    return input.read(reinterpret_cast<char*>(out_bytes->data()), size).good();
}

bool write_binary_file(const std::string& filename, const std::vector<uint8_t>& bytes) {
    if (filename.empty() || bytes.empty()) {
        return false;
    }

    std::ofstream output(filename, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

bool create_runtime_pipeline_cache(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime) {
    if (!runtime || !dispatch.create_pipeline_cache) {
        return true;
    }
    if (runtime->pipeline_cache != VK_NULL_HANDLE) {
        return true;
    }

    std::vector<uint8_t> initial_data;
    if (!runtime->pipeline_cache_path.empty()) {
        read_binary_file(runtime->pipeline_cache_path, &initial_data);
    }

    VkPipelineCacheCreateInfo cache_ci{};
    cache_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cache_ci.initialDataSize = initial_data.size();
    cache_ci.pInitialData = initial_data.empty() ? nullptr : initial_data.data();

    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
    VkResult result = dispatch.create_pipeline_cache(device, &cache_ci, nullptr, &pipeline_cache);
    if ((result != VK_SUCCESS || pipeline_cache == VK_NULL_HANDLE) && !initial_data.empty()) {
        cache_ci.initialDataSize = 0;
        cache_ci.pInitialData = nullptr;
        result = dispatch.create_pipeline_cache(device, &cache_ci, nullptr, &pipeline_cache);
    }
    if (result != VK_SUCCESS || pipeline_cache == VK_NULL_HANDLE) {
        return false;
    }

    runtime->pipeline_cache = pipeline_cache;
    runtime->pipeline_cache_dirty = false;
    return true;
}

void persist_runtime_pipeline_cache(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime) {
    if (!runtime ||
        runtime->pipeline_cache == VK_NULL_HANDLE ||
        !runtime->pipeline_cache_dirty ||
        runtime->pipeline_cache_path.empty() ||
        !dispatch.get_pipeline_cache_data) {
        return;
    }

    size_t data_size = 0;
    if (dispatch.get_pipeline_cache_data(device, runtime->pipeline_cache, &data_size, nullptr) != VK_SUCCESS ||
        data_size == 0) {
        return;
    }

    std::vector<uint8_t> data(data_size);
    if (dispatch.get_pipeline_cache_data(device, runtime->pipeline_cache, &data_size, data.data()) != VK_SUCCESS ||
        data_size == 0) {
        return;
    }
    data.resize(data_size);
    if (write_binary_file(runtime->pipeline_cache_path, data)) {
        runtime->pipeline_cache_dirty = false;
    }
}

bool create_compute_pipeline_for_kind(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    DecoderShaderKind kind,
    PipelineVariant variant) {
    if (!runtime) {
        return false;
    }

    VkPipeline* pipeline_slot = pipeline_slot_for_kind(runtime, kind, variant);
    const char* shader_name = shader_file_for_kind(kind);
    if (!pipeline_slot || !shader_name) {
        return false;
    }
    if (*pipeline_slot != VK_NULL_HANDLE) {
        return true;
    }

    std::vector<uint32_t> spirv_words;
    if (!read_spirv_file(shader_name, &spirv_words)) {
        return false;
    }

    VkShaderModuleCreateInfo shader_ci{};
    shader_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_ci.codeSize = spirv_words.size() * sizeof(uint32_t);
    shader_ci.pCode = spirv_words.data();

    VkShaderModule shader_module = VK_NULL_HANDLE;
    if (dispatch.create_shader_module(device, &shader_ci, nullptr, &shader_module) != VK_SUCCESS ||
        shader_module == VK_NULL_HANDLE) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stage_ci{};
    stage_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_ci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_ci.module = shader_module;
    stage_ci.pName = "main";

    bool tried_subgroup_variant = false;
    uint32_t requested_subgroup_size = 0;
#ifdef VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_ci{};
    if (variant == PipelineVariant::Wave32) {
        requested_subgroup_size = 32u;
    } else if (variant == PipelineVariant::Wave64) {
        requested_subgroup_size = 64u;
    }
    if (requested_subgroup_size == 32u || requested_subgroup_size == 64u) {
        subgroup_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
        subgroup_ci.requiredSubgroupSize = requested_subgroup_size;
        stage_ci.pNext = &subgroup_ci;
#ifdef VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT
        stage_ci.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT;
#endif
        tried_subgroup_variant = true;
    }
#endif

    VkComputePipelineCreateInfo pipeline_ci{};
    pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_ci.stage = stage_ci;
    pipeline_ci.layout = runtime->pipeline_layout;
    auto safe_pipeline_ci = clone_compute_pipeline_create_info(&pipeline_ci);

    if (requested_subgroup_size == 32u) {
        g_wave32_pipeline_tries.fetch_add(1);
    } else if (requested_subgroup_size == 64u) {
        g_wave64_pipeline_tries.fetch_add(1);
    }
    VkResult pipeline_result = dispatch.create_compute_pipelines(
        device, runtime->pipeline_cache, 1, safe_pipeline_ci.ptr(), nullptr, pipeline_slot);

    if ((pipeline_result != VK_SUCCESS || *pipeline_slot == VK_NULL_HANDLE) && tried_subgroup_variant) {
        stage_ci.pNext = nullptr;
        stage_ci.flags = 0;
        pipeline_ci.stage = stage_ci;
        safe_pipeline_ci = clone_compute_pipeline_create_info(&pipeline_ci);
        pipeline_result = dispatch.create_compute_pipelines(
            device, runtime->pipeline_cache, 1, safe_pipeline_ci.ptr(), nullptr, pipeline_slot);
    }

    dispatch.destroy_shader_module(device, shader_module, nullptr);
    if (pipeline_result != VK_SUCCESS || *pipeline_slot == VK_NULL_HANDLE) {
        *pipeline_slot = VK_NULL_HANDLE;
        return false;
    }
    if (runtime->pipeline_cache != VK_NULL_HANDLE) {
        runtime->pipeline_cache_dirty = true;
    }
    return true;
}

bool create_decode_descriptor_pool(
    VkDevice device,
    const DeviceDispatch& dispatch,
    uint32_t max_sets,
    VkDescriptorPool* out_pool) {
    if (!out_pool || !dispatch.create_descriptor_pool || max_sets == 0) {
        return false;
    }

    VkDescriptorPoolSize pool_sizes[3]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[0].descriptorCount = max_sets;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = max_sets;
    pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[2].descriptorCount = max_sets;

    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.maxSets = max_sets;
    pool_ci.poolSizeCount = 3;
    pool_ci.pPoolSizes = pool_sizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult result = dispatch.create_descriptor_pool(device, &pool_ci, nullptr, &pool);
    if (result != VK_SUCCESS || pool == VK_NULL_HANDLE) {
        return false;
    }

    *out_pool = pool;
    return true;
}

bool should_retry_descriptor_set_allocation(VkResult result) {
    return result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL;
}

VkDeviceSize align_descriptor_buffer_offset(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1) {
        return value;
    }
    VkDeviceSize remainder = value % alignment;
    return remainder == 0 ? value : (value + alignment - remainder);
}

VkDeviceAddress query_buffer_device_address(
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkBuffer buffer) {
    if (buffer == VK_NULL_HANDLE) {
        return 0;
    }

    VkBufferDeviceAddressInfo address_info{};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = buffer;

    if (dispatch.get_buffer_device_address) {
        return dispatch.get_buffer_device_address(device, &address_info);
    }
#ifdef VK_KHR_buffer_device_address
    if (dispatch.get_buffer_device_address_khr) {
        return dispatch.get_buffer_device_address_khr(device, &address_info);
    }
#endif
    return 0;
}

bool ensure_descriptor_buffer_storage(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaRuntime* vma_runtime) {
    if (!runtime || !runtime->use_descriptor_buffer) {
        return false;
    }
    if (runtime->descriptor_buffer != VK_NULL_HANDLE &&
        runtime->descriptor_buffer_allocation != VK_NULL_HANDLE &&
        runtime->descriptor_buffer_mapped != nullptr &&
        runtime->descriptor_buffer_address != 0) {
        return true;
    }
    if (!vma_runtime || vma_runtime->allocator == VK_NULL_HANDLE) {
        return false;
    }

    VkBufferCreateInfo buffer_ci{};
    buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_ci.size = kDescriptorBufferSizeBytes;
    buffer_ci.usage =
        VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_ci.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocationInfo alloc_info{};
    VkResult create_result = vmaCreateBuffer(
        vma_runtime->allocator,
        &buffer_ci,
        &alloc_ci,
        &buffer,
        &allocation,
        &alloc_info);
    if (create_result != VK_SUCCESS ||
        buffer == VK_NULL_HANDLE ||
        allocation == VK_NULL_HANDLE ||
        alloc_info.pMappedData == nullptr) {
        return false;
    }

    VkDeviceAddress address = query_buffer_device_address(device, dispatch, buffer);
    if (address == 0) {
        vmaDestroyBuffer(vma_runtime->allocator, buffer, allocation);
        return false;
    }

    runtime->descriptor_buffer = buffer;
    runtime->descriptor_buffer_allocation = allocation;
    runtime->descriptor_buffer_mapped = alloc_info.pMappedData;
    runtime->descriptor_buffer_address = address;
    runtime->descriptor_buffer_size = buffer_ci.size;
    runtime->descriptor_buffer_next_offset = 0;
    return true;
}

bool allocate_descriptor_buffer_region(
    ComputeRuntime* runtime,
    VkDeviceSize* out_offset) {
    if (!runtime || !runtime->use_descriptor_buffer || !out_offset) {
        return false;
    }

    VkDeviceSize aligned_offset = align_descriptor_buffer_offset(
        runtime->descriptor_buffer_next_offset,
        std::max<VkDeviceSize>(1, runtime->descriptor_buffer_offset_alignment));
    if (runtime->descriptor_buffer_layout_size == 0 ||
        aligned_offset + runtime->descriptor_buffer_layout_size > runtime->descriptor_buffer_size) {
        return false;
    }

    *out_offset = aligned_offset;
    runtime->descriptor_buffer_next_offset = aligned_offset + runtime->descriptor_buffer_layout_size;
    return true;
}

bool try_take_cached_decode_descriptor_set(
    ComputeRuntime* runtime,
    const DecodeDescriptorSetKey& key,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set) {
    if (!runtime || !out_pool || !out_set) {
        return false;
    }

    auto it = runtime->cached_decode_descriptor_sets.find(key);
    if (it == runtime->cached_decode_descriptor_sets.end() || it->second.empty()) {
        return false;
    }

    TrackedDescriptorSet tracked = it->second.back();
    it->second.pop_back();
    if (it->second.empty()) {
        runtime->cached_decode_descriptor_sets.erase(it);
    }

    *out_pool = tracked.pool;
    *out_set = tracked.set;
    runtime->descriptor_pool = tracked.pool;
    runtime->descriptor_set_cache_records[tracked.set] = DescriptorSetCacheRecord{
        DescriptorSetCacheKind::Decode,
        key,
        {},
    };
    return true;
}

bool try_take_cached_special_copy_descriptor_set(
    ComputeRuntime* runtime,
    const SpecialCopyDescriptorSetKey& key,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set) {
    if (!runtime || !out_pool || !out_set) {
        return false;
    }

    auto it = runtime->cached_special_copy_descriptor_sets.find(key);
    if (it == runtime->cached_special_copy_descriptor_sets.end() || it->second.empty()) {
        return false;
    }

    TrackedDescriptorSet tracked = it->second.back();
    it->second.pop_back();
    if (it->second.empty()) {
        runtime->cached_special_copy_descriptor_sets.erase(it);
    }

    *out_pool = tracked.pool;
    *out_set = tracked.set;
    runtime->descriptor_pool = tracked.pool;
    runtime->descriptor_set_cache_records[tracked.set] = DescriptorSetCacheRecord{
        DescriptorSetCacheKind::SpecialCopy,
        {},
        key,
    };
    return true;
}

}  // namespace

bool allocate_decode_descriptor_set(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set) {
    if (!runtime || !out_pool || !out_set || !dispatch.allocate_descriptor_sets) {
        return false;
    }

    std::lock_guard<std::mutex> pool_guard(runtime->descriptor_mutex);
    if (runtime->descriptor_set_layout == VK_NULL_HANDLE) {
        return false;
    }
    if (!runtime->recycled_descriptor_sets.empty()) {
        TrackedDescriptorSet recycled = runtime->recycled_descriptor_sets.back();
        runtime->recycled_descriptor_sets.pop_back();
        if (recycled.pool != VK_NULL_HANDLE && recycled.set != VK_NULL_HANDLE) {
            *out_pool = recycled.pool;
            *out_set = recycled.set;
            runtime->descriptor_pool = recycled.pool;
            return true;
        }
    }
    if (runtime->descriptor_pools.empty()) {
        if (runtime->descriptor_pool_capacity == 0) {
            runtime->descriptor_pool_capacity = kDescriptorPoolInitialMaxSetsFallback;
        }
        VkDescriptorPool initial_pool = VK_NULL_HANDLE;
        if (!create_decode_descriptor_pool(device, dispatch, runtime->descriptor_pool_capacity, &initial_pool)) {
            return false;
        }
        runtime->descriptor_pools.push_back(initial_pool);
        runtime->descriptor_pool = initial_pool;
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &runtime->descriptor_set_layout;

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    for (auto it = runtime->descriptor_pools.rbegin(); it != runtime->descriptor_pools.rend(); ++it) {
        alloc_info.descriptorPool = *it;
        descriptor_set = VK_NULL_HANDLE;
        VkResult alloc_result = dispatch.allocate_descriptor_sets(device, &alloc_info, &descriptor_set);
        if (alloc_result == VK_SUCCESS && descriptor_set != VK_NULL_HANDLE) {
            *out_pool = *it;
            *out_set = descriptor_set;
            runtime->descriptor_pool = *it;
            return true;
        }
        if (!should_retry_descriptor_set_allocation(alloc_result)) {
            return false;
        }
    }

    uint32_t base_capacity = runtime->descriptor_pool_capacity ? runtime->descriptor_pool_capacity : kDescriptorPoolInitialMaxSetsFallback;
    uint32_t next_capacity = base_capacity;
    uint32_t growth_cap = runtime->descriptor_pool_growth_cap ? runtime->descriptor_pool_growth_cap : (kDescriptorPoolInitialMaxSetsFallback * 16u);
    if (next_capacity < growth_cap) {
        next_capacity = std::min(growth_cap, next_capacity * 2u);
    } else {
        next_capacity += kDescriptorPoolInitialMaxSetsFallback;
    }

    VkDescriptorPool expanded_pool = VK_NULL_HANDLE;
    if (!create_decode_descriptor_pool(device, dispatch, next_capacity, &expanded_pool)) {
        return false;
    }
    runtime->descriptor_pools.push_back(expanded_pool);
    runtime->descriptor_pool = expanded_pool;
    runtime->descriptor_pool_capacity = next_capacity;

    alloc_info.descriptorPool = expanded_pool;
    descriptor_set = VK_NULL_HANDLE;
    VkResult alloc_result = dispatch.allocate_descriptor_sets(device, &alloc_info, &descriptor_set);
    if (alloc_result != VK_SUCCESS || descriptor_set == VK_NULL_HANDLE) {
        return false;
    }

    *out_pool = expanded_pool;
    *out_set = descriptor_set;
    return true;
}

bool prepare_decode_descriptor_set(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaRuntime* vma_runtime,
    VkImageView storage_view,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize range,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set) {
    if (!runtime || !out_pool || !out_set || !dispatch.update_descriptor_sets) {
        return false;
    }

    DescriptorWriteBuilder builder;
    builder.reset(VK_NULL_HANDLE);
    builder.add_storage_image(0, storage_view, VK_IMAGE_LAYOUT_GENERAL);
    builder.add_storage_buffer(1, buffer, offset, range);

    if (runtime->use_descriptor_buffer) {
        if (!dispatch.get_descriptor_ext ||
            !ensure_descriptor_buffer_storage(device, dispatch, runtime, vma_runtime)) {
            return false;
        }

        VkDeviceSize descriptor_offset = 0;
        if (!allocate_descriptor_buffer_region(runtime, &descriptor_offset)) {
            return false;
        }

        auto* descriptor_bytes =
            static_cast<uint8_t*>(runtime->descriptor_buffer_mapped) + descriptor_offset;
        if (!builder.write_descriptor_buffer(
                device,
                dispatch.get_descriptor_ext,
                runtime->descriptor_buffer_address,
                runtime->descriptor_buffer_binding_offsets,
                runtime->descriptor_buffer_binding_sizes,
                descriptor_bytes)) {
            return false;
        }

        *out_pool = VK_NULL_HANDLE;
        *out_set = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(descriptor_offset + 1));
        return true;
    }

    const DecodeDescriptorSetKey key{storage_view, buffer, offset, range};
    {
        std::lock_guard<std::mutex> pool_guard(runtime->descriptor_mutex);
        if (try_take_cached_decode_descriptor_set(runtime, key, out_pool, out_set)) {
            return true;
        }
    }

    if (!allocate_decode_descriptor_set(device, dispatch, runtime, out_pool, out_set)) {
        return false;
    }

    builder.reset(*out_set);
    dispatch.update_descriptor_sets(device, builder.write_count(), builder.writes(), 0, nullptr);

    {
        std::lock_guard<std::mutex> pool_guard(runtime->descriptor_mutex);
        runtime->descriptor_set_cache_records[*out_set] = DescriptorSetCacheRecord{
            DescriptorSetCacheKind::Decode,
            key,
            {},
        };
    }
    return true;
}

bool prepare_special_copy_descriptor_set(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaRuntime* vma_runtime,
    VkImageView dst_view,
    VkImageView src_view,
    VkSampler sampler,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set) {
    if (!runtime || !out_pool || !out_set || !dispatch.update_descriptor_sets) {
        return false;
    }

    DescriptorWriteBuilder builder;
    builder.reset(VK_NULL_HANDLE);
    builder.add_storage_image(0, dst_view, VK_IMAGE_LAYOUT_GENERAL);
    builder.add_combined_image_sampler(
        2,
        sampler,
        src_view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (runtime->use_descriptor_buffer) {
        if (!dispatch.get_descriptor_ext ||
            !ensure_descriptor_buffer_storage(device, dispatch, runtime, vma_runtime)) {
            return false;
        }

        VkDeviceSize descriptor_offset = 0;
        if (!allocate_descriptor_buffer_region(runtime, &descriptor_offset)) {
            return false;
        }

        auto* descriptor_bytes =
            static_cast<uint8_t*>(runtime->descriptor_buffer_mapped) + descriptor_offset;
        if (!builder.write_descriptor_buffer(
                device,
                dispatch.get_descriptor_ext,
                runtime->descriptor_buffer_address,
                runtime->descriptor_buffer_binding_offsets,
                runtime->descriptor_buffer_binding_sizes,
                descriptor_bytes)) {
            return false;
        }

        *out_pool = VK_NULL_HANDLE;
        *out_set = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(descriptor_offset + 1));
        return true;
    }

    const SpecialCopyDescriptorSetKey key{dst_view, src_view, sampler};
    {
        std::lock_guard<std::mutex> pool_guard(runtime->descriptor_mutex);
        if (try_take_cached_special_copy_descriptor_set(runtime, key, out_pool, out_set)) {
            return true;
        }
    }

    if (!allocate_decode_descriptor_set(device, dispatch, runtime, out_pool, out_set)) {
        return false;
    }

    builder.reset(*out_set);
    dispatch.update_descriptor_sets(device, builder.write_count(), builder.writes(), 0, nullptr);

    {
        std::lock_guard<std::mutex> pool_guard(runtime->descriptor_mutex);
        runtime->descriptor_set_cache_records[*out_set] = DescriptorSetCacheRecord{
            DescriptorSetCacheKind::SpecialCopy,
            {},
            key,
        };
    }
    return true;
}

bool initialize_compute_runtime(
    VkDevice device,
    const DeviceDispatch& dispatch,
    const ComputeRuntimeConfig& config,
    ComputeRuntime* runtime) {
    if (!runtime) {
        return false;
    }
    if (runtime->initialized) {
        return runtime->available;
    }

    runtime->initialized = true;
    runtime->available = false;
    runtime->pipeline_cache_path = config.pipeline_cache_path;
    runtime->pipeline_cache_dirty = false;
    runtime->use_descriptor_buffer =
        config.descriptor_buffer_supported &&
        dispatch.get_descriptor_set_layout_size_ext &&
        dispatch.get_descriptor_set_layout_binding_offset_ext &&
        dispatch.get_descriptor_ext &&
        dispatch.cmd_bind_descriptor_buffers_ext &&
        dispatch.cmd_set_descriptor_buffer_offsets_ext &&
        (dispatch.get_buffer_device_address || dispatch.get_buffer_device_address_khr) &&
        config.storage_image_descriptor_size != 0 &&
        config.storage_buffer_descriptor_size != 0 &&
        config.combined_image_sampler_descriptor_size != 0;
    runtime->descriptor_buffer_offset_alignment = config.descriptor_buffer_offset_alignment;
    runtime->descriptor_buffer_binding_sizes[0] = config.storage_image_descriptor_size;
    runtime->descriptor_buffer_binding_sizes[1] = config.storage_buffer_descriptor_size;
    runtime->descriptor_buffer_binding_sizes[2] = config.combined_image_sampler_descriptor_size;

    if (!dispatch.create_descriptor_set_layout ||
        !dispatch.destroy_descriptor_set_layout ||
        !dispatch.create_pipeline_layout ||
        !dispatch.destroy_pipeline_layout ||
        !dispatch.create_sampler ||
        !dispatch.destroy_sampler ||
        !dispatch.create_compute_pipelines ||
        !dispatch.destroy_pipeline ||
        !dispatch.create_descriptor_pool ||
        !dispatch.destroy_descriptor_pool ||
        !dispatch.allocate_descriptor_sets ||
        !dispatch.update_descriptor_sets ||
        !dispatch.create_shader_module ||
        !dispatch.destroy_shader_module ||
        !dispatch.cmd_bind_pipeline ||
        !dispatch.cmd_bind_descriptor_sets ||
        !dispatch.cmd_push_constants ||
        !dispatch.cmd_dispatch ||
        !dispatch.cmd_pipeline_barrier) {
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_ci{};
    dsl_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    if (runtime->use_descriptor_buffer) {
        dsl_ci.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }
    dsl_ci.bindingCount = 3;
    dsl_ci.pBindings = bindings;
    if (dispatch.create_descriptor_set_layout(device, &dsl_ci, nullptr, &runtime->descriptor_set_layout) != VK_SUCCESS ||
        runtime->descriptor_set_layout == VK_NULL_HANDLE) {
        return false;
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0;
    push_range.size = config.push_constant_size;

    VkPipelineLayoutCreateInfo pl_ci{};
    pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &runtime->descriptor_set_layout;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &push_range;
    if (dispatch.create_pipeline_layout(device, &pl_ci, nullptr, &runtime->pipeline_layout) != VK_SUCCESS ||
        runtime->pipeline_layout == VK_NULL_HANDLE) {
        return false;
    }

    if (runtime->use_descriptor_buffer) {
        dispatch.get_descriptor_set_layout_size_ext(
            device,
            runtime->descriptor_set_layout,
            &runtime->descriptor_buffer_layout_size);
        dispatch.get_descriptor_set_layout_binding_offset_ext(
            device,
            runtime->descriptor_set_layout,
            0,
            &runtime->descriptor_buffer_binding_offsets[0]);
        dispatch.get_descriptor_set_layout_binding_offset_ext(
            device,
            runtime->descriptor_set_layout,
            1,
            &runtime->descriptor_buffer_binding_offsets[1]);
        dispatch.get_descriptor_set_layout_binding_offset_ext(
            device,
            runtime->descriptor_set_layout,
            2,
            &runtime->descriptor_buffer_binding_offsets[2]);
    }

    VkSamplerCreateInfo sampler_ci{};
    sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.magFilter = VK_FILTER_NEAREST;
    sampler_ci.minFilter = VK_FILTER_NEAREST;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.minLod = 0.0f;
    sampler_ci.maxLod = 0.0f;
    sampler_ci.maxAnisotropy = 1.0f;
    auto safe_sampler_ci = clone_sampler_create_info(&sampler_ci);
    if (dispatch.create_sampler(device, safe_sampler_ci.ptr(), nullptr, &runtime->copy_sampler) != VK_SUCCESS ||
        runtime->copy_sampler == VK_NULL_HANDLE) {
        return false;
    }

    if (!create_runtime_pipeline_cache(device, dispatch, runtime)) {
        return false;
    }

    runtime->descriptor_pool_capacity =
        config.initial_descriptor_pool_capacity ? config.initial_descriptor_pool_capacity : kDescriptorPoolInitialMaxSetsFallback;
    runtime->descriptor_pool_growth_cap =
        config.descriptor_pool_growth_cap ? config.descriptor_pool_growth_cap : (kDescriptorPoolInitialMaxSetsFallback * 16u);
    runtime->descriptor_pools.clear();
    runtime->descriptor_pool = VK_NULL_HANDLE;
    if (!runtime->use_descriptor_buffer) {
        if (!create_decode_descriptor_pool(
                device,
                dispatch,
                runtime->descriptor_pool_capacity,
                &runtime->descriptor_pool)) {
            return false;
        }
        runtime->descriptor_pools.push_back(runtime->descriptor_pool);
    }
    runtime->preferred_subgroup_size = config.preferred_subgroup_size;
    runtime->supports_wave32 = config.supports_wave32;
    runtime->supports_wave64 = config.supports_wave64;

    runtime->available = false;
    uint32_t warmed_pipelines = 0;
    for (uint32_t kind_value = static_cast<uint32_t>(DecoderShaderKind::S3tc);
         kind_value < static_cast<uint32_t>(DecoderShaderKind::Count);
         ++kind_value) {
        const DecoderShaderKind kind = static_cast<DecoderShaderKind>(kind_value);
        bool ok_default = create_compute_pipeline_for_kind(
            device, dispatch, runtime, kind, PipelineVariant::Default);
        warmed_pipelines += ok_default ? 1u : 0u;
        bool ok_wave32 = false;
        bool ok_wave64 = false;
        if (runtime->supports_wave32) {
            ok_wave32 = create_compute_pipeline_for_kind(
                device, dispatch, runtime, kind, PipelineVariant::Wave32);
            warmed_pipelines += ok_wave32 ? 1u : 0u;
        }
        if (runtime->supports_wave64) {
            ok_wave64 = create_compute_pipeline_for_kind(
                device, dispatch, runtime, kind, PipelineVariant::Wave64);
            warmed_pipelines += ok_wave64 ? 1u : 0u;
        }
        runtime->available = runtime->available || ok_default || ok_wave32 || ok_wave64;
    }
    EXYNOS_LOGI(
        "Compute runtime prewarmed %u BCn/copy pipelines (wave32=%d wave64=%d descriptorBuffer=%d cache=%s).",
        warmed_pipelines,
        runtime->supports_wave32 ? 1 : 0,
        runtime->supports_wave64 ? 1 : 0,
        runtime->use_descriptor_buffer ? 1 : 0,
        runtime->pipeline_cache_path.empty() ? "disabled" : runtime->pipeline_cache_path.c_str());
    return runtime->available;
}

void destroy_compute_runtime(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaRuntime* vma_runtime) {
    if (!runtime) {
        return;
    }

    auto destroy_pipeline_slot = [&](VkPipeline& pipeline) {
        if (pipeline != VK_NULL_HANDLE && dispatch.destroy_pipeline) {
            dispatch.destroy_pipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    };
    for (auto& pipeline_variants : runtime->pipelines) {
        for (VkPipeline& pipeline : pipeline_variants) {
            destroy_pipeline_slot(pipeline);
        }
    }

    persist_runtime_pipeline_cache(device, dispatch, runtime);
    if (runtime->pipeline_cache != VK_NULL_HANDLE && dispatch.destroy_pipeline_cache) {
        dispatch.destroy_pipeline_cache(device, runtime->pipeline_cache, nullptr);
        runtime->pipeline_cache = VK_NULL_HANDLE;
    }

    if (dispatch.destroy_descriptor_pool) {
        std::lock_guard<std::mutex> descriptor_guard(runtime->descriptor_mutex);
        runtime->recycled_descriptor_sets.clear();
        runtime->cached_decode_descriptor_sets.clear();
        runtime->cached_special_copy_descriptor_sets.clear();
        runtime->descriptor_set_cache_records.clear();
        for (VkDescriptorPool pool : runtime->descriptor_pools) {
            if (pool != VK_NULL_HANDLE) {
                dispatch.destroy_descriptor_pool(device, pool, nullptr);
            }
        }
        runtime->descriptor_pools.clear();
        runtime->descriptor_pool = VK_NULL_HANDLE;
        runtime->descriptor_pool_capacity = 0;
        runtime->descriptor_pool_growth_cap = 0;
    }
    if (runtime->descriptor_buffer != VK_NULL_HANDLE &&
        runtime->descriptor_buffer_allocation != VK_NULL_HANDLE &&
        vma_runtime &&
        vma_runtime->allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(
            vma_runtime->allocator,
            runtime->descriptor_buffer,
            runtime->descriptor_buffer_allocation);
        runtime->descriptor_buffer_mapped = nullptr;
        runtime->descriptor_buffer = VK_NULL_HANDLE;
        runtime->descriptor_buffer_allocation = VK_NULL_HANDLE;
        runtime->descriptor_buffer_address = 0;
        runtime->descriptor_buffer_size = 0;
        runtime->descriptor_buffer_next_offset = 0;
    }
    if (runtime->pipeline_layout != VK_NULL_HANDLE && dispatch.destroy_pipeline_layout) {
        dispatch.destroy_pipeline_layout(device, runtime->pipeline_layout, nullptr);
        runtime->pipeline_layout = VK_NULL_HANDLE;
    }
    if (runtime->descriptor_set_layout != VK_NULL_HANDLE && dispatch.destroy_descriptor_set_layout) {
        dispatch.destroy_descriptor_set_layout(device, runtime->descriptor_set_layout, nullptr);
        runtime->descriptor_set_layout = VK_NULL_HANDLE;
    }
    if (runtime->copy_sampler != VK_NULL_HANDLE && dispatch.destroy_sampler) {
        dispatch.destroy_sampler(device, runtime->copy_sampler, nullptr);
        runtime->copy_sampler = VK_NULL_HANDLE;
    }
    runtime->pipeline_cache_path.clear();
    runtime->preferred_subgroup_size = 0;
    runtime->supports_wave32 = false;
    runtime->supports_wave64 = false;
    runtime->initialized = false;
    runtime->available = false;
}

void release_descriptor_sets(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    std::vector<TrackedDescriptorSet>* descriptor_sets) {
    if (!descriptor_sets || descriptor_sets->empty()) {
        return;
    }
    if (runtime) {
        std::lock_guard<std::mutex> pool_guard(runtime->descriptor_mutex);
        for (const TrackedDescriptorSet& tracked : *descriptor_sets) {
            if (tracked.pool == VK_NULL_HANDLE || tracked.set == VK_NULL_HANDLE) {
                continue;
            }
            auto record_it = runtime->descriptor_set_cache_records.find(tracked.set);
            if (record_it != runtime->descriptor_set_cache_records.end()) {
                const DescriptorSetCacheRecord& record = record_it->second;
                if (record.kind == DescriptorSetCacheKind::Decode) {
                    runtime->cached_decode_descriptor_sets[record.decode_key].push_back(tracked);
                    continue;
                }
                if (record.kind == DescriptorSetCacheKind::SpecialCopy) {
                    runtime->cached_special_copy_descriptor_sets[record.special_copy_key].push_back(tracked);
                    continue;
                }
            }
            runtime->recycled_descriptor_sets.push_back(tracked);
        }
        descriptor_sets->clear();
        return;
    }
    if (dispatch.free_descriptor_sets) {
        std::unordered_map<void*, VkDescriptorPool> pools_by_key;
        std::unordered_map<void*, std::vector<VkDescriptorSet>> sets_by_pool;
        for (const TrackedDescriptorSet& tracked : *descriptor_sets) {
            if (tracked.pool == VK_NULL_HANDLE || tracked.set == VK_NULL_HANDLE) {
                continue;
            }
            void* key = reinterpret_cast<void*>(tracked.pool);
            pools_by_key[key] = tracked.pool;
            sets_by_pool[key].push_back(tracked.set);
        }
        for (auto& kv : sets_by_pool) {
            auto pool_it = pools_by_key.find(kv.first);
            if (pool_it == pools_by_key.end() || pool_it->second == VK_NULL_HANDLE || kv.second.empty()) {
                continue;
            }
            dispatch.free_descriptor_sets(
                device,
                pool_it->second,
                static_cast<uint32_t>(kv.second.size()),
                kv.second.data());
        }
    }
    descriptor_sets->clear();
}
