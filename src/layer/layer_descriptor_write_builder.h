#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan.h>

class DescriptorWriteBuilder {
public:
    DescriptorWriteBuilder();

    void reset(VkDescriptorSet dst_set);
    void add_storage_image(uint32_t binding, VkImageView image_view, VkImageLayout image_layout);
    void add_storage_buffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
    void add_combined_image_sampler(
        uint32_t binding,
        VkSampler sampler,
        VkImageView image_view,
        VkImageLayout image_layout);
    bool write_descriptor_buffer(
        VkDevice device,
        PFN_vkGetDescriptorEXT get_descriptor_ext,
        VkDeviceAddress descriptor_buffer_base_address,
        const VkDeviceSize binding_offsets[3],
        const size_t binding_sizes[3],
        uint8_t* dst_bytes) const;

    uint32_t write_count() const;
    const VkWriteDescriptorSet* writes() const;

private:
    enum class EntryKind : uint32_t {
        StorageImage,
        StorageBuffer,
        CombinedImageSampler,
    };

    struct Entry {
        EntryKind kind = EntryKind::StorageImage;
        uint32_t binding = 0;
        uint32_t image_info_index = 0;
        uint32_t buffer_info_index = 0;
    };

    VkDescriptorSet dst_set_ = VK_NULL_HANDLE;
    uint32_t write_count_ = 0;
    uint32_t image_info_count_ = 0;
    uint32_t buffer_info_count_ = 0;
    uint32_t entry_count_ = 0;
    std::array<Entry, 3> entries_{};
    std::array<VkWriteDescriptorSet, 3> writes_{};
    std::array<VkDescriptorImageInfo, 3> image_infos_{};
    std::array<VkDescriptorBufferInfo, 2> buffer_infos_{};
};
