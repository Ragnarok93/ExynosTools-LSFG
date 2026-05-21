#pragma once

#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "layer_dispatch_types.h"

struct VirtualizationPolicySettings {
    bool enabled = true;
    bool xclipse_only = true;
    bool bcn_intercept = true;
    bool force_bcn_emulation = true;
    uint32_t disabled_bcn_mask = 0;
};

struct BcnSupportKey {
    void* physical = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;

    bool operator==(const BcnSupportKey& other) const {
        return physical == other.physical &&
               format == other.format &&
               type == other.type &&
               tiling == other.tiling &&
               usage == other.usage &&
               flags == other.flags;
    }
};

struct BcnSupportKeyHash {
    size_t operator()(const BcnSupportKey& key) const;
};

struct PatchedImageFormatListChain {
    VkImageFormatListCreateInfo patched_format_list{};
    std::vector<VkFormat> patched_view_formats;
    VkImageFormatListCreateInfo* original_format_list = nullptr;
    VkBaseOutStructure* previous = nullptr;
    bool replaced_head = false;
    bool inserted_head = false;
};

bool is_bcn_format(VkFormat format);

bool is_bcn_srgb_format(VkFormat format);

bool is_bcn_unorm_srgb_pair_format(VkFormat format);

VkFormat bcn_srgb_variant(VkFormat format);

VkFormat bcn_unorm_variant(VkFormat format);

VkFormat bcn_fallback_replacement_format(VkFormat format);

VkFormat srgb_view_compatible_format(VkFormat format);

VkFormat unorm_view_compatible_format(VkFormat format);

VkImageCreateFlags bcn_replacement_image_create_flags(VkImageType type, VkImageCreateFlags flags);

VkFormat bcn_replacement_format(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags);

bool is_native_bcn_supported(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    std::shared_mutex& lock,
    std::unordered_map<BcnSupportKey, bool, BcnSupportKeyHash>& native_support_cache);

bool should_virtualize_bcn_format(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    const VirtualizationPolicySettings& settings,
    bool is_xclipse_physical,
    std::shared_mutex& lock,
    std::unordered_map<BcnSupportKey, bool, BcnSupportKeyHash>& native_support_cache);

bool can_virtualize_bcn_image_create_info(const VkImageCreateInfo* create_info);

bool patch_virtualized_image_format_list(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    const VkImageCreateInfo* original_info,
    VkImageCreateInfo* io_patched_info,
    PatchedImageFormatListChain* out_patch);

void restore_virtualized_image_format_list(
    VkImageCreateInfo* io_patched_info,
    PatchedImageFormatListChain* patch);
