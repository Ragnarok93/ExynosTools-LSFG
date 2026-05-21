#include "layer_descriptor_write_builder.h"

#include <cstring>

namespace {

void initialize_write(
    VkWriteDescriptorSet* write,
    VkDescriptorSet dst_set,
    uint32_t binding,
    VkDescriptorType type) {
    std::memset(write, 0, sizeof(*write));
    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write->dstSet = dst_set;
    write->dstBinding = binding;
    write->descriptorType = type;
    write->descriptorCount = 1;
}

}  // namespace

DescriptorWriteBuilder::DescriptorWriteBuilder() = default;

void DescriptorWriteBuilder::reset(VkDescriptorSet dst_set) {
    dst_set_ = dst_set;
    write_count_ = 0;
    image_info_count_ = 0;
    buffer_info_count_ = 0;
    entry_count_ = 0;
}

void DescriptorWriteBuilder::add_storage_image(
    uint32_t binding,
    VkImageView image_view,
    VkImageLayout image_layout) {
    const uint32_t image_info_index = image_info_count_++;
    VkDescriptorImageInfo* image_info = &image_infos_[image_info_index];
    std::memset(image_info, 0, sizeof(*image_info));
    image_info->imageView = image_view;
    image_info->imageLayout = image_layout;

    VkWriteDescriptorSet* write = &writes_[write_count_++];
    initialize_write(write, dst_set_, binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    write->pImageInfo = image_info;

    Entry* entry = &entries_[entry_count_++];
    entry->kind = EntryKind::StorageImage;
    entry->binding = binding;
    entry->image_info_index = image_info_index;
}

void DescriptorWriteBuilder::add_storage_buffer(
    uint32_t binding,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize range) {
    const uint32_t buffer_info_index = buffer_info_count_++;
    VkDescriptorBufferInfo* buffer_info = &buffer_infos_[buffer_info_index];
    std::memset(buffer_info, 0, sizeof(*buffer_info));
    buffer_info->buffer = buffer;
    buffer_info->offset = offset;
    buffer_info->range = range;

    VkWriteDescriptorSet* write = &writes_[write_count_++];
    initialize_write(write, dst_set_, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    write->pBufferInfo = buffer_info;

    Entry* entry = &entries_[entry_count_++];
    entry->kind = EntryKind::StorageBuffer;
    entry->binding = binding;
    entry->buffer_info_index = buffer_info_index;
}

void DescriptorWriteBuilder::add_combined_image_sampler(
    uint32_t binding,
    VkSampler sampler,
    VkImageView image_view,
    VkImageLayout image_layout) {
    const uint32_t image_info_index = image_info_count_++;
    VkDescriptorImageInfo* image_info = &image_infos_[image_info_index];
    std::memset(image_info, 0, sizeof(*image_info));
    image_info->sampler = sampler;
    image_info->imageView = image_view;
    image_info->imageLayout = image_layout;

    VkWriteDescriptorSet* write = &writes_[write_count_++];
    initialize_write(write, dst_set_, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    write->pImageInfo = image_info;

    Entry* entry = &entries_[entry_count_++];
    entry->kind = EntryKind::CombinedImageSampler;
    entry->binding = binding;
    entry->image_info_index = image_info_index;
}

bool DescriptorWriteBuilder::write_descriptor_buffer(
    VkDevice device,
    PFN_vkGetDescriptorEXT get_descriptor_ext,
    VkDeviceAddress descriptor_buffer_base_address,
    const VkDeviceSize binding_offsets[3],
    const size_t binding_sizes[3],
    uint8_t* dst_bytes) const {
    if (!get_descriptor_ext || !binding_offsets || !binding_sizes || !dst_bytes) {
        return false;
    }

    for (uint32_t i = 0; i < entry_count_; ++i) {
        const Entry& entry = entries_[i];
        switch (entry.kind) {
            case EntryKind::StorageImage: {
                const VkDescriptorImageInfo& image_info = image_infos_[entry.image_info_index];
                VkDescriptorGetInfoEXT get_info{};
                get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
                get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                get_info.data.pStorageImage = &image_info;
                get_descriptor_ext(
                    device,
                    &get_info,
                    binding_sizes[entry.binding],
                    dst_bytes + binding_offsets[entry.binding]);
                break;
            }
            case EntryKind::StorageBuffer: {
                const VkDescriptorBufferInfo& buffer_info = buffer_infos_[entry.buffer_info_index];
                VkDescriptorAddressInfoEXT address_info{};
                address_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
                address_info.address = descriptor_buffer_base_address + buffer_info.offset;
                address_info.range = buffer_info.range;

                VkDescriptorGetInfoEXT get_info{};
                get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
                get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                get_info.data.pStorageBuffer = &address_info;
                get_descriptor_ext(
                    device,
                    &get_info,
                    binding_sizes[entry.binding],
                    dst_bytes + binding_offsets[entry.binding]);
                break;
            }
            case EntryKind::CombinedImageSampler: {
                const VkDescriptorImageInfo& image_info = image_infos_[entry.image_info_index];
                VkDescriptorGetInfoEXT get_info{};
                get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
                get_info.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                get_info.data.pCombinedImageSampler = &image_info;
                get_descriptor_ext(
                    device,
                    &get_info,
                    binding_sizes[entry.binding],
                    dst_bytes + binding_offsets[entry.binding]);
                break;
            }
        }
    }
    return true;
}

uint32_t DescriptorWriteBuilder::write_count() const {
    return write_count_;
}

const VkWriteDescriptorSet* DescriptorWriteBuilder::writes() const {
    return writes_.data();
}
