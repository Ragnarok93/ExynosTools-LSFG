#include "layer_staging_allocations.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace {

constexpr VkDeviceSize kDefaultStagingChunkSize = 4ull * 1024ull * 1024ull;
constexpr VkDeviceSize kCpuUploadStagingChunkSize = 8ull * 1024ull * 1024ull;
constexpr VkDeviceSize kMaxPooledCpuUploadSize = 16ull * 1024ull * 1024ull;

VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1) {
        return value;
    }
    VkDeviceSize remainder = value % alignment;
    return remainder == 0 ? value : (value + alignment - remainder);
}

bool allocate_from_chunk(
    StagingChunk* chunk,
    VkDeviceSize byte_size,
    VkBufferUsageFlags required_usage,
    VkDeviceSize alignment,
    StagingAllocation* out_staging) {
    if (!chunk || !out_staging) {
        return false;
    }
    if ((chunk->usage & required_usage) != required_usage) {
        return false;
    }

    for (size_t i = 0; i < chunk->free_ranges.size(); ++i) {
        const StagingChunkRange range = chunk->free_ranges[i];
        VkDeviceSize aligned_offset = align_up(range.offset, alignment);
        VkDeviceSize padding = aligned_offset - range.offset;
        if (range.size < padding || range.size - padding < byte_size) {
            continue;
        }

        StagingChunkRange prefix{range.offset, padding};
        StagingChunkRange suffix{
            aligned_offset + byte_size,
            range.size - padding - byte_size,
        };

        chunk->free_ranges.erase(
            chunk->free_ranges.begin() + static_cast<std::ptrdiff_t>(i));
        if (suffix.size != 0) {
            chunk->free_ranges.insert(
                chunk->free_ranges.begin() + static_cast<std::ptrdiff_t>(i),
                suffix);
        }
        if (prefix.size != 0) {
            chunk->free_ranges.insert(
                chunk->free_ranges.begin() + static_cast<std::ptrdiff_t>(i),
                prefix);
        }

        *out_staging = StagingAllocation{
            chunk->buffer,
            chunk->allocation,
            aligned_offset,
            byte_size,
        };
        return true;
    }

    return false;
}

void merge_chunk_free_ranges(StagingChunk* chunk) {
    if (!chunk || chunk->free_ranges.size() < 2) {
        return;
    }

    std::sort(
        chunk->free_ranges.begin(),
        chunk->free_ranges.end(),
        [](const StagingChunkRange& lhs, const StagingChunkRange& rhs) {
            return lhs.offset < rhs.offset;
        });

    size_t write_index = 0;
    for (size_t read_index = 1; read_index < chunk->free_ranges.size(); ++read_index) {
        StagingChunkRange& current = chunk->free_ranges[write_index];
        const StagingChunkRange& next = chunk->free_ranges[read_index];
        if (current.offset + current.size >= next.offset) {
            VkDeviceSize end = std::max(
                current.offset + current.size,
                next.offset + next.size);
            current.size = end - current.offset;
        } else {
            ++write_index;
            chunk->free_ranges[write_index] = next;
        }
    }
    chunk->free_ranges.resize(write_index + 1);
}

bool create_staging_chunk(
    VmaRuntime* runtime,
    VkDeviceSize minimum_size,
    VkDeviceSize alignment,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memory_usage,
    VmaAllocationCreateFlags allocation_flags,
    VkDeviceSize default_chunk_size,
    StagingChunk* out_chunk) {
    if (!runtime || runtime->allocator == VK_NULL_HANDLE || !out_chunk) {
        return false;
    }

    VkDeviceSize chunk_size = std::max(
        align_up(minimum_size, alignment),
        align_up(default_chunk_size, alignment));

    VkBufferCreateInfo buffer_ci{};
    buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_ci.size = chunk_size;
    buffer_ci.usage = usage;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = memory_usage;
    alloc_ci.flags = allocation_flags;

    StagingChunk chunk{};
    VmaAllocationInfo alloc_info{};
    VkResult create_result = vmaCreateBuffer(
        runtime->allocator,
        &buffer_ci,
        &alloc_ci,
        &chunk.buffer,
        &chunk.allocation,
        &alloc_info);
    if (create_result != VK_SUCCESS ||
        chunk.buffer == VK_NULL_HANDLE ||
        chunk.allocation == VK_NULL_HANDLE) {
        return false;
    }

    chunk.size = chunk_size;
    chunk.usage = usage;
    chunk.mapped_data = alloc_info.pMappedData;
    chunk.free_ranges.push_back(StagingChunkRange{0, chunk_size});
    *out_chunk = chunk;
    return true;
}

}  // namespace

bool create_staging_copy_for_region(
    VmaRuntime* runtime,
    VkDeviceSize byte_size,
    StagingAllocation* out_staging) {
    if (!out_staging || !runtime || runtime->allocator == VK_NULL_HANDLE) {
        return false;
    }
    if (byte_size == 0) {
        return false;
    }

    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        16,
        runtime->min_storage_buffer_offset_alignment);

    std::lock_guard<std::mutex> guard(runtime->staging_mutex);
    for (StagingChunk& chunk : runtime->staging_chunks) {
        if (allocate_from_chunk(
                &chunk,
                byte_size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                alignment,
                out_staging)) {
            return true;
        }
    }

    StagingChunk new_chunk{};
    if (!create_staging_chunk(
            runtime,
            byte_size,
            alignment,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            0,
            kDefaultStagingChunkSize,
            &new_chunk)) {
        return false;
    }

    runtime->staging_chunks.push_back(std::move(new_chunk));
    return allocate_from_chunk(
        &runtime->staging_chunks.back(),
        byte_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        alignment,
        out_staging);
}

bool create_cpu_upload_staging_for_region(
    VmaRuntime* runtime,
    VkDeviceSize byte_size,
    const void* data,
    StagingAllocation* out_staging) {
    if (!out_staging || !runtime || runtime->allocator == VK_NULL_HANDLE || !data || byte_size == 0) {
        return false;
    }

    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        16,
        runtime->min_storage_buffer_offset_alignment);

    if (byte_size <= kMaxPooledCpuUploadSize) {
        std::lock_guard<std::mutex> guard(runtime->staging_mutex);
        for (StagingChunk& chunk : runtime->staging_chunks) {
            if (!chunk.mapped_data) {
                continue;
            }
            if (allocate_from_chunk(
                    &chunk,
                    byte_size,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    alignment,
                    out_staging)) {
                auto* mapped = static_cast<uint8_t*>(chunk.mapped_data);
                if (!mapped) {
                    return false;
                }
                std::memcpy(mapped + out_staging->offset, data, static_cast<size_t>(byte_size));
                vmaFlushAllocation(runtime->allocator, chunk.allocation, out_staging->offset, byte_size);
                return true;
            }
        }

        StagingChunk new_chunk{};
        if (create_staging_chunk(
                runtime,
                byte_size,
                alignment,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT,
                kCpuUploadStagingChunkSize,
                &new_chunk)) {
            if (!new_chunk.mapped_data) {
                vmaDestroyBuffer(runtime->allocator, new_chunk.buffer, new_chunk.allocation);
                return false;
            }
            runtime->staging_chunks.push_back(std::move(new_chunk));
            StagingChunk& chunk = runtime->staging_chunks.back();
            if (allocate_from_chunk(
                    &chunk,
                    byte_size,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    alignment,
                    out_staging)) {
                auto* mapped = static_cast<uint8_t*>(chunk.mapped_data);
                if (!mapped) {
                    return false;
                }
                std::memcpy(mapped + out_staging->offset, data, static_cast<size_t>(byte_size));
                vmaFlushAllocation(runtime->allocator, chunk.allocation, out_staging->offset, byte_size);
                return true;
            }
        }
    }

    VkBufferCreateInfo buffer_ci{};
    buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_ci.size = byte_size;
    buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_ci.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo alloc_info{};
    VkResult result = vmaCreateBuffer(
        runtime->allocator,
        &buffer_ci,
        &alloc_ci,
        &buffer,
        &allocation,
        &alloc_info);
    if (result != VK_SUCCESS ||
        buffer == VK_NULL_HANDLE ||
        allocation == VK_NULL_HANDLE ||
        alloc_info.pMappedData == nullptr) {
        if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
            vmaDestroyBuffer(runtime->allocator, buffer, allocation);
        }
        return false;
    }

    std::memcpy(alloc_info.pMappedData, data, static_cast<size_t>(byte_size));
    vmaFlushAllocation(runtime->allocator, allocation, 0, byte_size);

    *out_staging = StagingAllocation{buffer, allocation, 0, byte_size, true};
    return true;
}

void release_staging_allocations(
    VmaRuntime* runtime,
    std::vector<StagingAllocation>* allocations) {
    if (!allocations) {
        return;
    }
    if (!runtime || runtime->allocator == VK_NULL_HANDLE) {
        allocations->clear();
        return;
    }

    std::lock_guard<std::mutex> guard(runtime->staging_mutex);
    for (const StagingAllocation& staging : *allocations) {
        if (staging.buffer == VK_NULL_HANDLE ||
            staging.allocation == VK_NULL_HANDLE ||
            staging.size == 0) {
            continue;
        }

        if (staging.dedicated) {
            vmaDestroyBuffer(runtime->allocator, staging.buffer, staging.allocation);
            continue;
        }

        auto it = std::find_if(
            runtime->staging_chunks.begin(),
            runtime->staging_chunks.end(),
            [&staging](const StagingChunk& chunk) {
                return chunk.buffer == staging.buffer &&
                       chunk.allocation == staging.allocation;
            });
        if (it == runtime->staging_chunks.end()) {
            continue;
        }

        it->free_ranges.push_back(StagingChunkRange{staging.offset, staging.size});
        merge_chunk_free_ranges(&(*it));
    }
    allocations->clear();
}
