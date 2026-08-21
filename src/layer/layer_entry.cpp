#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <vulkan/utility/vk_safe_struct.hpp>
#if defined(__ANDROID__)
#include <android/hardware_buffer.h>
#include <vulkan/vulkan_android.h>
#endif

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "exynostools_embedded_spirv.h"
#include "layer_bcn_cpu_decoder.h"
#include "layer_command_buffer_hooks.h"
#include "layer_copy_image_routing.h"
#include "layer_compute_runtime.h"
#include "layer_command_buffer_resources.h"
#include "layer_command_buffer_ownership.h"
#include "layer_dispatch_key.h"
#include "layer_global_state.h"
#include "layer_logging.h"
#include "layer_device_dispatch_types.h"
#include "layer_dispatch_types.h"
#include "layer_format_virtualization.h"
#include "layer_image_virtualization.h"
#include "layer_pipeline_selection.h"
#include "layer_lsfg_compat.h"
#include "layer_settings_types.h"
#include "layer_settings_runtime.h"
#include "layer_settings_utils.h"
#include "layer_shared_types.h"
#include "layer_telemetry.h"
#include "layer_vma_runtime.h"
#include "layer_staging_allocations.h"
#include "layer_temp_arena.h"
#include "layer_vk_struct_clone.h"
#include "layer_vk_struct_utils.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

inline void log_copy_image_route_warning(const char* api_name, int src_format, int dst_format) {
    EXYNOS_LOGW(
        "%s hit a virtual image copy with mismatched actual formats (src=%d dst=%d). "
        "No translated special path succeeded, so the layer is blocking the native copy to avoid an invalid real-format copy.",
        api_name,
        src_format,
        dst_format);
}

inline void log_blit_image_route_warning(const char* api_name, int src_format, int dst_format) {
    EXYNOS_LOGW(
        "%s hit a virtual image blit with mismatched actual formats (src=%d dst=%d). "
        "No translated blit path exists yet, so the layer is blocking the native blit to avoid an invalid real-format blit.",
        api_name,
        src_format,
        dst_format);
}

inline bool should_block_native_virtual_image_transfer(const CopyImageRouteInfo& route) {
    return snapshot_layer_settings().block_incompatible_virtual_copies &&
           route.involves_virtual &&
           route.needs_special_path &&
           !route.can_copy_real_images;
}

inline void note_blit_image_route(const char* api_name, const CopyImageRouteInfo& route) {
    if (route.involves_virtual && !route.can_copy_real_images) {
        log_blit_image_route_warning(
            api_name,
            static_cast<int>(route.src_actual_format),
            static_cast<int>(route.dst_actual_format));
    }
    maybe_log_decode_stats();
}

struct GraphicsPipelineInspectionResult {
    uint32_t sanitized_empty_specialization_infos = 0;
    uint32_t sanitized_empty_specialization_map_arrays = 0;
    uint32_t sanitized_empty_specialization_data_blocks = 0;
    uint32_t removed_invalid_subgroup_size_infos = 0;
    uint32_t removed_rendering_pnext_infos = 0;
    uint32_t sanitized_zero_color_attachment_blend_states = 0;
    uint32_t clamped_rendering_color_blend_attachment_counts = 0;
    uint32_t sanitized_empty_dynamic_state_arrays = 0;
    uint32_t sanitized_dynamic_viewport_arrays = 0;
    uint32_t sanitized_dynamic_scissor_arrays = 0;
    uint32_t sanitized_empty_color_blend_arrays = 0;
};

GraphicsPipelineInspectionResult inspect_and_patch_graphics_pipeline_create_infos(
    ClonedGraphicsPipelineCreateInfos* cloned_infos,
    const DeviceRuntime* device_runtime) {
    GraphicsPipelineInspectionResult result{};
    if (!cloned_infos) {
        return result;
    }

    static std::atomic<bool> g_warned_empty_graphics_entry_point{false};
    static std::atomic<bool> g_warned_invalid_graphics_specialization{false};
    static std::atomic<bool> g_warned_graphics_rendering_mismatch{false};
    static std::atomic<bool> g_warned_graphics_render_pass_rendering_mix{false};
    static std::atomic<bool> g_warned_graphics_zero_color_attachment_blend_state{false};
    static std::atomic<bool> g_warned_graphics_clamped_color_blend_attachments{false};
    static std::atomic<bool> g_warned_graphics_zero_viewport_count{false};
    static std::atomic<bool> g_warned_graphics_zero_scissor_count{false};
    static std::atomic<bool> g_warned_unsupported_geometry_stage{false};
    static std::atomic<bool> g_warned_unsupported_tessellation_stage{false};
    static std::atomic<bool> g_warned_invalid_graphics_subgroup_size{false};

    const bool supports_geometry = !device_runtime || device_runtime->geometry_shader;
    const bool supports_tessellation = !device_runtime || device_runtime->tessellation_shader;
    const bool supports_subgroup_size_control = device_runtime && device_runtime->subgroup_size_control;

    for (VkGraphicsPipelineCreateInfo& create_info : cloned_infos->infos) {
        auto has_dynamic_state = [&](VkDynamicState dynamic_state) {
            const VkPipelineDynamicStateCreateInfo* dynamic_state_info = create_info.pDynamicState;
            if (!dynamic_state_info || !dynamic_state_info->pDynamicStates) {
                return false;
            }
            for (uint32_t i = 0; i < dynamic_state_info->dynamicStateCount; ++i) {
                if (dynamic_state_info->pDynamicStates[i] == dynamic_state) {
                    return true;
                }
            }
            return false;
        };

        const bool has_dynamic_viewport =
            has_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
#ifdef VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT
            || has_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT)
#endif
            ;
        const bool has_dynamic_scissor =
            has_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
#ifdef VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT
            || has_dynamic_state(VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT)
#endif
            ;

        if (create_info.pDynamicState &&
            create_info.pDynamicState->dynamicStateCount == 0 &&
            create_info.pDynamicState->pDynamicStates != nullptr) {
            auto* dynamic_state =
                const_cast<VkPipelineDynamicStateCreateInfo*>(create_info.pDynamicState);
            dynamic_state->pDynamicStates = nullptr;
            ++result.sanitized_empty_dynamic_state_arrays;
        }

        if (create_info.pViewportState) {
            auto* viewport_state =
                const_cast<VkPipelineViewportStateCreateInfo*>(create_info.pViewportState);

            if (has_dynamic_viewport && viewport_state->pViewports != nullptr) {
                viewport_state->pViewports = nullptr;
                ++result.sanitized_dynamic_viewport_arrays;
            } else if (!has_dynamic_viewport &&
                       viewport_state->viewportCount == 0 &&
                       !g_warned_graphics_zero_viewport_count.exchange(true)) {
                EXYNOS_LOGW(
                    "Graphics pipeline arrived with viewportCount=0 without a dynamic viewport state. "
                    "The layer is forwarding it unchanged after inspection.");
            }

            if (has_dynamic_scissor && viewport_state->pScissors != nullptr) {
                viewport_state->pScissors = nullptr;
                ++result.sanitized_dynamic_scissor_arrays;
            } else if (!has_dynamic_scissor &&
                       viewport_state->scissorCount == 0 &&
                       !g_warned_graphics_zero_scissor_count.exchange(true)) {
                EXYNOS_LOGW(
                    "Graphics pipeline arrived with scissorCount=0 without a dynamic scissor state. "
                    "The layer is forwarding it unchanged after inspection.");
            }
        }

        if (create_info.pColorBlendState &&
            create_info.pColorBlendState->attachmentCount == 0 &&
            create_info.pColorBlendState->pAttachments != nullptr) {
            auto* color_blend_state =
                const_cast<VkPipelineColorBlendStateCreateInfo*>(create_info.pColorBlendState);
            color_blend_state->pAttachments = nullptr;
            ++result.sanitized_empty_color_blend_arrays;
        }

#ifdef VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO
        auto* rendering_info = find_struct_in_pnext_chain<VkPipelineRenderingCreateInfo>(
            const_cast<void*>(create_info.pNext),
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        if (rendering_info && create_info.renderPass != VK_NULL_HANDLE) {
            remove_struct_from_cloned_pnext_chain<VkPipelineRenderingCreateInfo>(
                &create_info.pNext,
                VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
            ++result.removed_rendering_pnext_infos;
            rendering_info = nullptr;
            if (!g_warned_graphics_render_pass_rendering_mix.exchange(true)) {
                EXYNOS_LOGW(
                    "Graphics pipeline mixed renderPass with VkPipelineRenderingCreateInfo in pNext. "
                    "The layer removed the dynamic rendering pNext before forwarding to the driver.");
            }
        }

        if (rendering_info &&
            rendering_info->colorAttachmentCount == 0 &&
            create_info.pColorBlendState &&
            create_info.pColorBlendState->attachmentCount != 0) {
            auto* color_blend_state =
                const_cast<VkPipelineColorBlendStateCreateInfo*>(create_info.pColorBlendState);
            color_blend_state->attachmentCount = 0;
            color_blend_state->pAttachments = nullptr;
            ++result.sanitized_zero_color_attachment_blend_states;
            if (!g_warned_graphics_zero_color_attachment_blend_state.exchange(true)) {
                EXYNOS_LOGW(
                    "Graphics pipeline reported zero dynamic rendering color attachments but kept color blend attachments. "
                    "The layer cleared the color blend attachment array before forwarding to the driver.");
            }
        }

        if (rendering_info &&
            create_info.pColorBlendState &&
            create_info.pColorBlendState->attachmentCount != 0) {
            auto* color_blend_state =
                const_cast<VkPipelineColorBlendStateCreateInfo*>(create_info.pColorBlendState);
            if (rendering_info->colorAttachmentCount < color_blend_state->attachmentCount) {
                color_blend_state->attachmentCount = rendering_info->colorAttachmentCount;
                ++result.clamped_rendering_color_blend_attachment_counts;
                if (!g_warned_graphics_clamped_color_blend_attachments.exchange(true)) {
                    EXYNOS_LOGW(
                        "Graphics pipeline used more color blend attachments than dynamic rendering color attachments. "
                        "The layer clamped the color blend attachment count before forwarding to the driver.");
                }
            } else if (rendering_info->colorAttachmentCount != color_blend_state->attachmentCount &&
                       !g_warned_graphics_rendering_mismatch.exchange(true)) {
                EXYNOS_LOGW(
                    "Graphics pipeline uses dynamic rendering with colorAttachmentCount=%u but color blend attachmentCount=%u. "
                    "The layer is forwarding the cloned pipeline as-is.",
                    rendering_info->colorAttachmentCount,
                    color_blend_state->attachmentCount);
            }
        }
#endif

        for (uint32_t stage_index = 0; stage_index < create_info.stageCount; ++stage_index) {
            auto* stages = const_cast<VkPipelineShaderStageCreateInfo*>(create_info.pStages);
            if (!stages) {
                break;
            }
            VkPipelineShaderStageCreateInfo& stage = stages[stage_index];

            if ((!stage.pName || stage.pName[0] == '\0') &&
                !g_warned_empty_graphics_entry_point.exchange(true)) {
                EXYNOS_LOGW(
                    "Graphics pipeline stage %u arrived with an empty entry point. "
                    "The layer will forward it unchanged, but this is suspicious on Xclipse drivers.",
                    stage_index);
            }

            if (stage.stage == VK_SHADER_STAGE_GEOMETRY_BIT &&
                !supports_geometry &&
                !g_warned_unsupported_geometry_stage.exchange(true)) {
                EXYNOS_LOGW(
                    "Graphics pipeline requests a geometry shader stage on a device that did not advertise geometryShader. "
                    "The layer is forwarding it unchanged.");
            }

            if ((stage.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
                 stage.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) &&
                !supports_tessellation &&
                !g_warned_unsupported_tessellation_stage.exchange(true)) {
                EXYNOS_LOGW(
                    "Graphics pipeline requests tessellation stages on a device that did not advertise tessellationShader. "
                    "The layer is forwarding it unchanged.");
            }

            const VkSpecializationInfo* specialization = stage.pSpecializationInfo;
            if (!specialization) {
                continue;
            }

            auto* mutable_specialization =
                const_cast<VkSpecializationInfo*>(specialization);

            if (specialization->mapEntryCount == 0 && specialization->dataSize == 0) {
                stage.pSpecializationInfo = nullptr;
                ++result.sanitized_empty_specialization_infos;
                continue;
            }

            if (specialization->mapEntryCount == 0 && specialization->pMapEntries != nullptr) {
                mutable_specialization->pMapEntries = nullptr;
                ++result.sanitized_empty_specialization_map_arrays;
            }
            if (specialization->dataSize == 0 && specialization->pData != nullptr) {
                mutable_specialization->pData = nullptr;
                ++result.sanitized_empty_specialization_data_blocks;
            }

            if (((specialization->mapEntryCount != 0) && !specialization->pMapEntries) ||
                ((specialization->dataSize != 0) && !specialization->pData)) {
                if (!g_warned_invalid_graphics_specialization.exchange(true)) {
                    EXYNOS_LOGW(
                        "Graphics pipeline stage specialization info looks incomplete (missing map entries or data pointer). "
                        "The layer is forwarding it unchanged.");
                }
            }

#ifdef VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO
            auto* subgroup_size_info =
                find_struct_in_pnext_chain<VkPipelineShaderStageRequiredSubgroupSizeCreateInfo>(
                    const_cast<void*>(stage.pNext),
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO);
            if (subgroup_size_info) {
                bool invalid_subgroup_size = (subgroup_size_info->requiredSubgroupSize == 0);
                if (!supports_subgroup_size_control) {
                    invalid_subgroup_size = true;
                } else {
                    if (device_runtime->min_subgroup_size != 0 &&
                        subgroup_size_info->requiredSubgroupSize < device_runtime->min_subgroup_size) {
                        invalid_subgroup_size = true;
                    }
                    if (device_runtime->max_subgroup_size != 0 &&
                        subgroup_size_info->requiredSubgroupSize > device_runtime->max_subgroup_size) {
                        invalid_subgroup_size = true;
                    }
                }

                if (invalid_subgroup_size) {
                    remove_struct_from_cloned_pnext_chain<VkPipelineShaderStageRequiredSubgroupSizeCreateInfo>(
                        &stage.pNext,
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO);
#ifdef VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT
                    stage.flags &= ~VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT;
#endif
                    ++result.removed_invalid_subgroup_size_infos;
                    if (!g_warned_invalid_graphics_subgroup_size.exchange(true)) {
                        EXYNOS_LOGW(
                            "Graphics pipeline stage requested an unsupported required subgroup size through pNext. "
                            "The layer removed that stage pNext before forwarding to the driver.");
                    }
                }
            }
#endif
        }
    }

    return result;
}

#if defined(__GNUC__)
#define EXYNOS_LAYER_EXPORT __attribute__((visibility("default")))
#else
#define EXYNOS_LAYER_EXPORT
#endif

namespace {
const char* kLayerName = "VK_LAYER_VORTEK_XCLIPSE";
const uint32_t kLayerImplVersion = 300u;
constexpr uint32_t kDecodePushConstantsSize = sizeof(int32_t) * 8u;
constexpr uint32_t kDescriptorPoolInitialMaxSets = 4096u;
constexpr uint32_t kDescriptorPoolMaxSetsCap = 65536u;
constexpr uint32_t kDescriptorPoolInitialMaxSetsHighEnd = 8192u;
constexpr uint32_t kDecodeBlockedRetryInterval = 64u;
constexpr const char* kPipelineCacheFileName = "vortek_xclipse_pipeline_cache.bin";
constexpr bool kEnableDescriptorBufferFastPath = false;

bool is_xclipse_physical(void* physical_key);

void record_virtualized_bcn_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            g_virtualized_bcn_bc1.fetch_add(1);
            break;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
            g_virtualized_bcn_bc2.fetch_add(1);
            break;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            g_virtualized_bcn_bc3.fetch_add(1);
            break;
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            g_virtualized_bcn_bc4.fetch_add(1);
            break;
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
            g_virtualized_bcn_bc5.fetch_add(1);
            break;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            g_virtualized_bcn_bc6.fetch_add(1);
            break;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            g_virtualized_bcn_bc7.fetch_add(1);
            break;
        default:
            break;
    }
    if (is_bcn_srgb_format(format)) {
        g_virtualized_bcn_srgb.fetch_add(1);
    }
}

struct VmaRuntimeInitInputs {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    InstanceDispatch instance_dispatch{};
};

std::string dirname_copy_local(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return std::string();
    }
    return path.substr(0, slash);
}

std::string join_path_copy_local(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty()) {
        return rhs;
    }
    char last = lhs.back();
    if (last == '/' || last == '\\') {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

std::string default_pipeline_cache_path() {
    if (const char* env_path = std::getenv("VORTEK_XCLIPSE_PIPELINE_CACHE_PATH")) {
        if (*env_path != '\0') {
            return env_path;
        }
    }
    if (const char* config_path = std::getenv("VORTEK_XCLIPSE_CACHE_DIR")) {
        if (*config_path != '\0') {
            std::string dir = dirname_copy_local(config_path);
            if (!dir.empty()) {
                return join_path_copy_local(dir, kPipelineCacheFileName);
            }
        }
    }
#if defined(__ANDROID__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&default_pipeline_cache_path), &info) != 0 && info.dli_fname) {
        std::string dir = dirname_copy_local(info.dli_fname);
        if (!dir.empty()) {
            return join_path_copy_local(dir, kPipelineCacheFileName);
        }
    }
#endif
    return std::string();
}

ComputeRuntimeConfig compute_runtime_config_for_device(VkDevice device) {
    ComputeRuntimeConfig config{};
    config.push_constant_size = kDecodePushConstantsSize;
    config.initial_descriptor_pool_capacity = kDescriptorPoolInitialMaxSets;
    config.descriptor_pool_growth_cap = kDescriptorPoolMaxSetsCap;
    config.preferred_subgroup_size = 0;
    config.pipeline_cache_path = default_pipeline_cache_path();

    std::shared_lock<std::shared_mutex> guard(g_lock);
    auto it_runtime = g_device_runtime.find(dispatch_key(device));
    if (it_runtime == g_device_runtime.end()) {
        return config;
    }

    const DeviceRuntime& device_runtime = it_runtime->second;
    if (device_runtime.descriptor_buffer_supported) {
        config.initial_descriptor_pool_capacity = std::max(
            config.initial_descriptor_pool_capacity,
            kDescriptorPoolInitialMaxSetsHighEnd);
    }
    config.descriptor_buffer_supported =
        kEnableDescriptorBufferFastPath && device_runtime.descriptor_buffer_supported;
    config.descriptor_buffer_offset_alignment = device_runtime.descriptor_buffer_offset_alignment;
    config.storage_image_descriptor_size = device_runtime.storage_image_descriptor_size;
    config.storage_buffer_descriptor_size = device_runtime.storage_buffer_descriptor_size;
    config.combined_image_sampler_descriptor_size =
        device_runtime.combined_image_sampler_descriptor_size;
    if (device_runtime.subgroup_size_control) {
        bool has_wave32 =
            (device_runtime.min_subgroup_size != 0) &&
            (device_runtime.min_subgroup_size <= 32u) &&
            (device_runtime.max_subgroup_size >= 32u);
        bool has_wave64 =
            (device_runtime.min_subgroup_size != 0) &&
            (device_runtime.min_subgroup_size <= 64u) &&
            (device_runtime.max_subgroup_size >= 64u);
        config.supports_wave32 = has_wave32;
        config.supports_wave64 = has_wave64;
        if (has_wave32) {
            config.preferred_subgroup_size = 32u;
        } else if (has_wave64) {
            config.preferred_subgroup_size = 64u;
        }
    }
    return config;
}

bool gather_vma_runtime_init_inputs(void* device_key, VmaRuntimeInitInputs* out_inputs) {
    if (!out_inputs) {
        return false;
    }

    std::shared_lock<std::shared_mutex> guard(g_lock);
    auto it_instance_handle = g_device_to_instance_handle.find(device_key);
    auto it_physical_handle = g_device_to_physical_handle.find(device_key);
    if (it_instance_handle == g_device_to_instance_handle.end() ||
        it_physical_handle == g_device_to_physical_handle.end()) {
        return false;
    }

    auto it_instance_dispatch = g_instance_dispatch.find(dispatch_key(it_instance_handle->second));
    if (it_instance_dispatch == g_instance_dispatch.end()) {
        return false;
    }

    out_inputs->instance = it_instance_handle->second;
    out_inputs->physical_device = it_physical_handle->second;
    out_inputs->instance_dispatch = it_instance_dispatch->second;
    return true;
}

CommandBufferDispatchContext make_command_buffer_dispatch_context() {
    return CommandBufferDispatchContext{
        g_lock,
        g_device_dispatch,
        []() { return snapshot_layer_settings(); },
        [](const char* api_name) {
            if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
                EXYNOS_LOGW("Command buffer mapping missing for %s.", api_name);
            }
        },
        [](const char* api_name) {
            if (!g_warned_cmd_buffer_dispatch_drop.exchange(true)) {
                EXYNOS_LOGW(
                    "Dropping %s because command buffer mapping is missing and strict dispatch is enabled.",
                    api_name);
            }
        },
        [](const char* api_name) {
            if (!g_warned_cmd_buffer_dispatch_fallback.exchange(true)) {
                EXYNOS_LOGW(
                    "Falling back to single-device dispatch for %s without command buffer mapping.",
                    api_name);
            }
        },
    };
}

CommandBufferHookContext make_command_buffer_hook_context() {
    return CommandBufferHookContext{
        g_lock,
        g_device_dispatch,
        g_compute_runtime,
        g_vma_runtime,
    };
}

bool has_enabled_device_extension(
    const VkDeviceCreateInfo* pCreateInfo,
    const char* extension_name) {
    if (!pCreateInfo || !extension_name || !pCreateInfo->ppEnabledExtensionNames) {
        return false;
    }
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char* name = pCreateInfo->ppEnabledExtensionNames[i];
        if (name && std::strcmp(name, extension_name) == 0) {
            return true;
        }
    }
    return false;
}

std::string lowercase_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains_case_insensitive(const std::string& haystack, const char* needle) {
    if (!needle || !needle[0]) {
        return false;
    }
    return lowercase_copy(haystack).find(lowercase_copy(needle)) != std::string::npos;
}

InstanceRuntime make_instance_runtime(const VkInstanceCreateInfo* create_info) {
    InstanceRuntime runtime{};
    const VkApplicationInfo* app_info = create_info ? create_info->pApplicationInfo : nullptr;
    if (!app_info) {
        return runtime;
    }

    runtime.application_name = app_info->pApplicationName ? app_info->pApplicationName : "";
    runtime.application_version = app_info->applicationVersion;
    runtime.engine_name = app_info->pEngineName ? app_info->pEngineName : "";
    runtime.engine_version = app_info->engineVersion;
    runtime.api_version = app_info->apiVersion;

    runtime.is_dxvk = contains_case_insensitive(runtime.engine_name, "dxvk");
    runtime.is_dxvk_2_or_newer =
        runtime.is_dxvk &&
        VK_VERSION_MAJOR(runtime.engine_version) >= 2;
    runtime.is_vkd3d_proton =
        contains_case_insensitive(runtime.engine_name, "vkd3d") ||
        contains_case_insensitive(runtime.engine_name, "vkd3d-proton") ||
        contains_case_insensitive(runtime.application_name, "vkd3d");
    runtime.is_clvk =
        contains_case_insensitive(runtime.engine_name, "clvk") ||
        contains_case_insensitive(runtime.application_name, "clvk");
    return runtime;
}

VkDriverId query_physical_driver_id(
    VkPhysicalDevice physical_device,
    const InstanceDispatch& dispatch) {
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES
    VkPhysicalDeviceDriverProperties driver_props{};
    driver_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &driver_props;

    if (dispatch.get_physical_device_properties2) {
        dispatch.get_physical_device_properties2(physical_device, &props2);
        return driver_props.driverID;
    }
#ifdef VK_KHR_get_physical_device_properties2
    if (dispatch.get_physical_device_properties2_khr) {
        dispatch.get_physical_device_properties2_khr(
            physical_device,
            reinterpret_cast<VkPhysicalDeviceProperties2KHR*>(&props2));
        return driver_props.driverID;
    }
#endif
#else
    (void)physical_device;
    (void)dispatch;
#endif
    return VK_DRIVER_ID_MAX_ENUM;
}

bool should_hide_device_extension(
    const PhysicalRuntime& runtime,
    const InstanceRuntime* app_runtime,
    const char* extension_name) {
    if (!extension_name || runtime.is_xclipse) {
        return false;
    }
    if (runtime.driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY &&
        std::strcmp(extension_name, "VK_KHR_shader_float_controls") == 0) {
        return true;
    }
    if (runtime.driver_id == VK_DRIVER_ID_ARM_PROPRIETARY &&
        app_runtime &&
        !app_runtime->is_dxvk_2_or_newer &&
        (std::strcmp(extension_name, "VK_EXT_extended_dynamic_state") == 0 ||
         std::strcmp(extension_name, "VK_EXT_extended_dynamic_state2") == 0 ||
         std::strcmp(extension_name, "VK_EXT_extended_dynamic_state3") == 0)) {
        return true;
    }
    return false;
}

bool should_hide_device_extension(
    const PhysicalRuntime& runtime,
    const char* extension_name) {
    return should_hide_device_extension(runtime, nullptr, extension_name);
}

bool get_physical_runtime_snapshot(
    VkPhysicalDevice physical_device,
    PhysicalRuntime* out_runtime) {
    if (!out_runtime) {
        return false;
    }
    std::shared_lock<std::shared_mutex> guard(g_lock);
    auto it = g_physical_runtime.find(dispatch_key(physical_device));
    if (it == g_physical_runtime.end()) {
        return false;
    }
    *out_runtime = it->second;
    return true;
}

bool device_create_requests_descriptor_buffer_feature(const VkDeviceCreateInfo* create_info) {
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
    auto* mutable_info = const_cast<VkDeviceCreateInfo*>(create_info);
    auto* features = find_struct_in_pnext_chain<VkPhysicalDeviceDescriptorBufferFeaturesEXT>(
        const_cast<void*>(mutable_info->pNext),
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT);
    return features && features->descriptorBuffer == VK_TRUE;
#else
    (void)create_info;
    return false;
#endif
}

bool device_create_requests_buffer_device_address_feature(const VkDeviceCreateInfo* create_info) {
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
    auto* mutable_info = const_cast<VkDeviceCreateInfo*>(create_info);
    auto* features = find_struct_in_pnext_chain<VkPhysicalDeviceBufferDeviceAddressFeatures>(
        const_cast<void*>(mutable_info->pNext),
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
    if (features && features->bufferDeviceAddress == VK_TRUE) {
        return true;
    }
#endif
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR
    auto* mutable_info = const_cast<VkDeviceCreateInfo*>(create_info);
    auto* features_khr = find_struct_in_pnext_chain<VkPhysicalDeviceBufferDeviceAddressFeaturesKHR>(
        const_cast<void*>(mutable_info->pNext),
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR);
    if (features_khr && features_khr->bufferDeviceAddress == VK_TRUE) {
        return true;
    }
#endif
    return false;
}

struct DescriptorBufferCreateSupport {
    bool extension_supported = false;
    bool descriptor_buffer_feature_supported = false;
    bool buffer_device_address_feature_supported = false;
};

DescriptorBufferCreateSupport query_descriptor_buffer_create_support(
    VkPhysicalDevice physical_device,
    VkInstance instance,
    const InstanceDispatch& dispatch) {
    DescriptorBufferCreateSupport support{};
    if (physical_device == VK_NULL_HANDLE || instance == VK_NULL_HANDLE || !dispatch.get_instance_proc_addr) {
        return support;
    }

    auto enumerate_device_extension_properties =
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            dispatch.get_instance_proc_addr(instance, "vkEnumerateDeviceExtensionProperties"));
    if (enumerate_device_extension_properties) {
        uint32_t property_count = 0;
        if (enumerate_device_extension_properties(physical_device, nullptr, &property_count, nullptr) == VK_SUCCESS &&
            property_count != 0) {
            std::vector<VkExtensionProperties> properties(property_count);
            if (enumerate_device_extension_properties(
                    physical_device,
                    nullptr,
                    &property_count,
                    properties.data()) == VK_SUCCESS) {
                for (const VkExtensionProperties& property : properties) {
                    if (std::strcmp(property.extensionName, "VK_EXT_descriptor_buffer") == 0) {
                        support.extension_supported = true;
                        break;
                    }
                }
            }
        }
    }

    if (!support.extension_supported) {
        return support;
    }

    auto get_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        dispatch.get_instance_proc_addr(instance, "vkGetPhysicalDeviceFeatures2"));
#ifdef VK_KHR_get_physical_device_properties2
    auto get_features2_khr = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2KHR>(
        dispatch.get_instance_proc_addr(instance, "vkGetPhysicalDeviceFeatures2KHR"));
#endif

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features{};
    descriptor_buffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    prepend_struct_to_pnext_chain(&features2.pNext, &descriptor_buffer_features);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features{};
    buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    prepend_struct_to_pnext_chain(&features2.pNext, &buffer_device_address_features);
#elif defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR)
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address_features{};
    buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
    prepend_struct_to_pnext_chain(&features2.pNext, &buffer_device_address_features);
#endif

    if (get_features2) {
        get_features2(physical_device, &features2);
#ifdef VK_KHR_get_physical_device_properties2
    } else if (get_features2_khr) {
        get_features2_khr(physical_device, reinterpret_cast<VkPhysicalDeviceFeatures2KHR*>(&features2));
#endif
    } else {
        return support;
    }

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
    support.descriptor_buffer_feature_supported = (descriptor_buffer_features.descriptorBuffer == VK_TRUE);
#endif
#if defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES) || \
    defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR)
    support.buffer_device_address_feature_supported = (buffer_device_address_features.bufferDeviceAddress == VK_TRUE);
#endif
    return support;
}

bool is_xclipse_device(void* device_key) {
    auto it = g_device_runtime.find(device_key);
    return it != g_device_runtime.end() && it->second.is_xclipse;
}

bool is_xclipse_physical(void* physical_key) {
    auto it = g_physical_runtime.find(physical_key);
    return it != g_physical_runtime.end() && it->second.is_xclipse;
}

bool get_instance_dispatch_for_physical(
    VkPhysicalDevice physicalDevice,
    InstanceDispatch* out_dispatch,
    VkInstance* out_instance) {
    if (!out_dispatch) {
        return false;
    }

    std::shared_lock<std::shared_mutex> guard(g_lock);
    auto phys_key = dispatch_key(physicalDevice);
    auto it_map = g_physical_to_instance.find(phys_key);
    if (it_map == g_physical_to_instance.end()) {
        return false;
    }
    auto it_inst = g_instance_dispatch.find(it_map->second);
    if (it_inst == g_instance_dispatch.end()) {
        return false;
    }
    *out_dispatch = it_inst->second;
    if (out_instance) {
        auto it_inst_handle = g_physical_to_instance_handle.find(phys_key);
        *out_instance = (it_inst_handle != g_physical_to_instance_handle.end()) ? it_inst_handle->second : VK_NULL_HANDLE;
    }
    return true;
}

bool get_any_instance_dispatch(
    InstanceDispatch* out_dispatch,
    VkInstance* out_instance) {
    if (!out_dispatch) {
        return false;
    }

    std::shared_lock<std::shared_mutex> guard(g_lock);
    auto it = g_instance_dispatch.begin();
    if (it == g_instance_dispatch.end()) {
        return false;
    }

    *out_dispatch = it->second;
    if (out_instance) {
        *out_instance = reinterpret_cast<VkInstance>(it->first);
    }
    return true;
}

bool compute_virtual_bc_feature_enabled(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch) {
    static constexpr VkFormat kRepresentativeBcnFormats[] = {
        VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
        VK_FORMAT_BC3_UNORM_BLOCK,
        VK_FORMAT_BC5_UNORM_BLOCK,
        VK_FORMAT_BC6H_UFLOAT_BLOCK,
        VK_FORMAT_BC7_UNORM_BLOCK,
    };
    constexpr VkImageUsageFlags kRepresentativeUsage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    for (VkFormat format : kRepresentativeBcnFormats) {
        if (should_virtualize_bcn_format(
                physicalDevice,
                dispatch,
                format,
                VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL,
                kRepresentativeUsage,
                0,
                snapshot_virtualization_policy_settings(),
                is_xclipse_physical(dispatch_key(physicalDevice)),
                g_lock,
                g_bcn_native_support_cache)) {
            return true;
        }
    }
    return false;
}

bool should_advertise_virtual_bc_feature(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch) {
    if (physicalDevice == VK_NULL_HANDLE) {
        return false;
    }

    const void* physical_key = dispatch_key(physicalDevice);
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_physical_runtime.find(const_cast<void*>(physical_key));
        if (it != g_physical_runtime.end() && it->second.virtual_bc_feature_cached) {
            return it->second.virtual_bc_feature_enabled;
        }
    }

    bool advertise = compute_virtual_bc_feature_enabled(physicalDevice, dispatch);
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto it = g_physical_runtime.find(const_cast<void*>(physical_key));
        if (it != g_physical_runtime.end()) {
            it->second.virtual_bc_feature_cached = true;
            it->second.virtual_bc_feature_enabled = advertise;
        }
    }
    return advertise;
}

void virtualize_physical_device_features_if_needed(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkPhysicalDeviceFeatures* io_features) {
    if (!io_features) {
        return;
    }
    if (should_advertise_virtual_bc_feature(physicalDevice, dispatch)) {
        io_features->textureCompressionBC = VK_TRUE;
    }
}

VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures* pFeatures) {
    if (!pFeatures) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !dispatch.get_physical_device_features) {
        std::memset(pFeatures, 0, sizeof(*pFeatures));
        return;
    }

    dispatch.get_physical_device_features(physicalDevice, pFeatures);
    virtualize_physical_device_features_if_needed(physicalDevice, dispatch, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures) {
    if (!pFeatures) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr)) {
        std::memset(&pFeatures->features, 0, sizeof(pFeatures->features));
        return;
    }

    if (dispatch.get_physical_device_features2) {
        dispatch.get_physical_device_features2(physicalDevice, pFeatures);
    } else if (dispatch.get_physical_device_features) {
        dispatch.get_physical_device_features(physicalDevice, &pFeatures->features);
    } else {
        std::memset(&pFeatures->features, 0, sizeof(pFeatures->features));
    }

    virtualize_physical_device_features_if_needed(physicalDevice, dispatch, &pFeatures->features);
}

#ifdef VK_KHR_get_physical_device_properties2
VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2KHR* pFeatures) {
    if (!pFeatures) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr)) {
        std::memset(&pFeatures->features, 0, sizeof(pFeatures->features));
        return;
    }

    if (dispatch.get_physical_device_features2_khr) {
        dispatch.get_physical_device_features2_khr(physicalDevice, pFeatures);
    } else if (dispatch.get_physical_device_features2) {
        dispatch.get_physical_device_features2(
            physicalDevice,
            reinterpret_cast<VkPhysicalDeviceFeatures2*>(pFeatures));
    } else if (dispatch.get_physical_device_features) {
        dispatch.get_physical_device_features(physicalDevice, &pFeatures->features);
    } else {
        std::memset(&pFeatures->features, 0, sizeof(pFeatures->features));
    }

    virtualize_physical_device_features_if_needed(physicalDevice, dispatch, &pFeatures->features);
}
#endif

void virtualize_format_properties_if_needed(
    VkPhysicalDevice physicalDevice,
    VkFormat requested_format,
    VkFormatProperties* io_props) {
    if (!io_props || !is_bcn_format(requested_format)) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !dispatch.get_physical_device_format_properties) {
        return;
    }

    if (!should_virtualize_bcn_format(
            physicalDevice,
            dispatch,
            requested_format,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            0,
            snapshot_virtualization_policy_settings(),
            is_xclipse_physical(dispatch_key(physicalDevice)),
            g_lock,
            g_bcn_native_support_cache)) {
        return;
    }

    VkFormat replacement = bcn_replacement_format(
        physicalDevice,
        dispatch,
        requested_format,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        0);
    if (replacement == VK_FORMAT_UNDEFINED) {
        return;
    }

    VkFormatProperties replacement_props{};
    dispatch.get_physical_device_format_properties(physicalDevice, replacement, &replacement_props);
    io_props->linearTilingFeatures = replacement_props.linearTilingFeatures;
    io_props->optimalTilingFeatures = replacement_props.optimalTilingFeatures;
    io_props->bufferFeatures = replacement_props.bufferFeatures;
}

struct DecodePushConstants {
    int32_t format;
    int32_t width;
    int32_t height;
    int32_t offset;
    int32_t bufferRowLength;
    int32_t offsetX;
    int32_t offsetY;
    int32_t reserved0;
};
static_assert(sizeof(DecodePushConstants) == kDecodePushConstantsSize, "DecodePushConstants layout mismatch.");

struct CopyImagePushConstants {
    int32_t srcOffsetX;
    int32_t srcOffsetY;
    int32_t dstOffsetX;
    int32_t dstOffsetY;
    int32_t width;
    int32_t height;
    int32_t reserved0;
    int32_t reserved1;
};
static_assert(sizeof(CopyImagePushConstants) == kDecodePushConstantsSize, "CopyImagePushConstants layout mismatch.");

struct PreparedDecodeRegion {
    DecoderShaderKind shader_kind = DecoderShaderKind::None;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkImageView storage_view = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    StagingAllocation staging{};
    VkDeviceSize src_offset = 0;
    VkDeviceSize byte_size = 0;
    uint32_t storage_view_layer = 0;
    VkImageSubresourceRange subresource_range{};
    DecodePushConstants regs{};
    uint32_t groups_x = 0;
    uint32_t groups_y = 0;
};

struct PreparedSpecialCopyRegion {
    DecoderShaderKind shader_kind = DecoderShaderKind::None;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkImageView src_view = VK_NULL_HANDLE;
    VkImageView dst_view = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkImageSubresourceRange src_subresource_range{};
    VkImageSubresourceRange dst_subresource_range{};
    CopyImagePushConstants regs{};
    uint32_t groups_x = 0;
    uint32_t groups_y = 0;
};

uint32_t block_size_bytes(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 8u;
        default:
            return 16u;
    }
}

VkPipelineStageFlags stage_mask_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
        default:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

VkAccessFlags access_mask_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return VK_ACCESS_HOST_WRITE_BIT;
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            return 0;
    }
}

VkPipelineStageFlags2 stage_mask2_from_legacy(VkPipelineStageFlags stages) {
    return static_cast<VkPipelineStageFlags2>(stages);
}

VkAccessFlags2 access_mask2_from_legacy(VkAccessFlags access) {
    return static_cast<VkAccessFlags2>(access);
}

void submit_pipeline_barrier_image(
    const DeviceDispatch& dispatch,
    VkCommandBuffer command_buffer,
    VkPipelineStageFlags src_stage_mask,
    VkPipelineStageFlags dst_stage_mask,
    uint32_t image_barrier_count,
    const VkImageMemoryBarrier* image_barriers) {
    if ((dispatch.cmd_pipeline_barrier2 || dispatch.cmd_pipeline_barrier2_khr) && image_barrier_count > 0 && image_barriers) {
        std::vector<VkImageMemoryBarrier2> barriers2(image_barrier_count);
        for (uint32_t i = 0; i < image_barrier_count; ++i) {
            const VkImageMemoryBarrier& src = image_barriers[i];
            VkImageMemoryBarrier2& dst = barriers2[i];
            dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            dst.srcStageMask = stage_mask2_from_legacy(src_stage_mask);
            dst.srcAccessMask = access_mask2_from_legacy(src.srcAccessMask);
            dst.dstStageMask = stage_mask2_from_legacy(dst_stage_mask);
            dst.dstAccessMask = access_mask2_from_legacy(src.dstAccessMask);
            dst.oldLayout = src.oldLayout;
            dst.newLayout = src.newLayout;
            dst.srcQueueFamilyIndex = src.srcQueueFamilyIndex;
            dst.dstQueueFamilyIndex = src.dstQueueFamilyIndex;
            dst.image = src.image;
            dst.subresourceRange = src.subresourceRange;
        }

        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.imageMemoryBarrierCount = image_barrier_count;
        dependency_info.pImageMemoryBarriers = barriers2.data();
        if (dispatch.cmd_pipeline_barrier2) {
            dispatch.cmd_pipeline_barrier2(command_buffer, &dependency_info);
        }
#ifdef VK_KHR_synchronization2
        else if (dispatch.cmd_pipeline_barrier2_khr) {
            dispatch.cmd_pipeline_barrier2_khr(command_buffer, &dependency_info);
        }
#endif
        return;
    }

    dispatch.cmd_pipeline_barrier(
        command_buffer,
        src_stage_mask,
        dst_stage_mask,
        0,
        0, nullptr,
        0, nullptr,
        image_barrier_count, image_barriers);
}

void submit_pipeline_barrier_buffer(
    const DeviceDispatch& dispatch,
    VkCommandBuffer command_buffer,
    VkPipelineStageFlags src_stage_mask,
    VkPipelineStageFlags dst_stage_mask,
    uint32_t buffer_barrier_count,
    const VkBufferMemoryBarrier* buffer_barriers) {
    if ((dispatch.cmd_pipeline_barrier2 || dispatch.cmd_pipeline_barrier2_khr) && buffer_barrier_count > 0 && buffer_barriers) {
        std::vector<VkBufferMemoryBarrier2> barriers2(buffer_barrier_count);
        for (uint32_t i = 0; i < buffer_barrier_count; ++i) {
            const VkBufferMemoryBarrier& src = buffer_barriers[i];
            VkBufferMemoryBarrier2& dst = barriers2[i];
            dst.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            dst.srcStageMask = stage_mask2_from_legacy(src_stage_mask);
            dst.srcAccessMask = access_mask2_from_legacy(src.srcAccessMask);
            dst.dstStageMask = stage_mask2_from_legacy(dst_stage_mask);
            dst.dstAccessMask = access_mask2_from_legacy(src.dstAccessMask);
            dst.srcQueueFamilyIndex = src.srcQueueFamilyIndex;
            dst.dstQueueFamilyIndex = src.dstQueueFamilyIndex;
            dst.buffer = src.buffer;
            dst.offset = src.offset;
            dst.size = src.size;
        }

        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = buffer_barrier_count;
        dependency_info.pBufferMemoryBarriers = barriers2.data();
        if (dispatch.cmd_pipeline_barrier2) {
            dispatch.cmd_pipeline_barrier2(command_buffer, &dependency_info);
        }
#ifdef VK_KHR_synchronization2
        else if (dispatch.cmd_pipeline_barrier2_khr) {
            dispatch.cmd_pipeline_barrier2_khr(command_buffer, &dependency_info);
        }
#endif
        return;
    }

    dispatch.cmd_pipeline_barrier(
        command_buffer,
        src_stage_mask,
        dst_stage_mask,
        0,
        0, nullptr,
        buffer_barrier_count, buffer_barriers,
        0, nullptr);
}

bool get_or_create_storage_view(
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkImage image,
    uint32_t mip_level,
    uint32_t layer,
    VkFormat format,
    VkImageView* out_view) {
    if (!out_view) {
        return false;
    }

    StorageViewKey key{};
    key.image = dispatch_key(image);
    key.mip_level = mip_level;
    key.layer = layer;
    key.format = format;

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_storage_views.find(key);
        if (it != g_storage_views.end()) {
            *out_view = it->second;
            return true;
        }
    }

    if (!dispatch.create_image_view) {
        return false;
    }

    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = format;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.baseMipLevel = mip_level;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.baseArrayLayer = layer;
    view_ci.subresourceRange.layerCount = 1;

    VkImageView created_view = VK_NULL_HANDLE;
    if (dispatch.create_image_view(device, &view_ci, nullptr, &created_view) != VK_SUCCESS ||
        created_view == VK_NULL_HANDLE) {
        return false;
    }

    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto insert_result = g_storage_views.emplace(key, created_view);
        if (!insert_result.second) {
            if (dispatch.destroy_image_view) {
                dispatch.destroy_image_view(device, created_view, nullptr);
            }
            created_view = insert_result.first->second;
        }
    }

    *out_view = created_view;
    return true;
}

bool shader_kind_requires_unformatted_storage(DecoderShaderKind kind) {
    return false;
}

bool build_decode_region_plan(
    const ComputeRuntime& runtime,
    VkFormat requested_format,
    VkFormat real_format,
    const VkBufferImageCopy& region,
    VkImageType image_type,
    uint32_t layer_index,
    uint32_t depth_slice_index,
    PreparedDecodeRegion* out_prepared) {
    if (!out_prepared || !runtime.available) {
        return false;
    }
    const bool is_3d_image = image_type == VK_IMAGE_TYPE_3D;
    if (image_type != VK_IMAGE_TYPE_2D && image_type != VK_IMAGE_TYPE_3D) {
        return false;
    }

    DecoderShaderKind shader_kind = shader_kind_for_decode(requested_format, real_format);
    VkPipeline pipeline = choose_decoder_pipeline(runtime, requested_format, real_format);
    if (pipeline == VK_NULL_HANDLE) {
        return false;
    }
    if (!is_3d_image && region.imageExtent.depth != 1) {
        return false;
    }
    if (is_3d_image &&
        (region.imageOffset.z < 0 ||
         region.imageExtent.depth == 0 ||
         region.imageSubresource.baseArrayLayer != 0 ||
         (region.imageSubresource.layerCount != 0 && region.imageSubresource.layerCount != 1))) {
        return false;
    }
    if (!is_3d_image && region.imageOffset.z != 0) {
        return false;
    }

    const uint32_t block_size = block_size_bytes(requested_format);
    uint32_t blocks_x = (std::max(region.bufferRowLength, region.imageExtent.width) + 3u) / 4u;
    uint32_t rows = region.bufferImageHeight ? region.bufferImageHeight : region.imageExtent.height;
    uint32_t blocks_y = (rows + 3u) / 4u;
    VkDeviceSize slice_stride = static_cast<VkDeviceSize>(blocks_x) *
                                static_cast<VkDeviceSize>(blocks_y) *
                                static_cast<VkDeviceSize>(block_size);
    if (slice_stride == 0) {
        return false;
    }
    uint32_t copy_blocks_x = (region.imageExtent.width + 3u) / 4u;
    uint32_t copy_blocks_y = (region.imageExtent.height + 3u) / 4u;
    if (copy_blocks_x == 0 || copy_blocks_y == 0) {
        return false;
    }
    VkDeviceSize row_pitch = static_cast<VkDeviceSize>(blocks_x) * static_cast<VkDeviceSize>(block_size);
    VkDeviceSize active_row_bytes =
        static_cast<VkDeviceSize>(copy_blocks_x) * static_cast<VkDeviceSize>(block_size);
    VkDeviceSize copy_footprint =
        static_cast<VkDeviceSize>(copy_blocks_y - 1u) * row_pitch + active_row_bytes;
    if (copy_footprint == 0 || copy_footprint > slice_stride) {
        return false;
    }

    if (region.imageExtent.width > static_cast<uint32_t>(INT32_MAX) ||
        region.imageExtent.height > static_cast<uint32_t>(INT32_MAX) ||
        region.bufferRowLength > static_cast<uint32_t>(INT32_MAX) ||
        region.imageOffset.x < 0 ||
        region.imageOffset.y < 0) {
        EXYNOS_LOGW("Region exceeds push constant integer range.");
        return false;
    }

    PreparedDecodeRegion prepared{};
    const uint32_t copy_depth = is_3d_image ? region.imageExtent.depth : 1u;
    VkDeviceSize layer_stride = slice_stride * static_cast<VkDeviceSize>(copy_depth);
    if (is_3d_image &&
        static_cast<uint32_t>(region.imageOffset.z) > (UINT32_MAX - depth_slice_index)) {
        return false;
    }
    const uint32_t view_layer = is_3d_image
        ? static_cast<uint32_t>(region.imageOffset.z) + depth_slice_index
        : region.imageSubresource.baseArrayLayer + layer_index;
    prepared.shader_kind = shader_kind;
    prepared.pipeline = pipeline;
    prepared.src_offset = region.bufferOffset +
                          static_cast<VkDeviceSize>(layer_index) * layer_stride +
                          static_cast<VkDeviceSize>(depth_slice_index) * slice_stride;
    prepared.byte_size = copy_footprint;
    prepared.storage_view_layer = view_layer;
    prepared.subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prepared.subresource_range.baseMipLevel = region.imageSubresource.mipLevel;
    prepared.subresource_range.levelCount = 1;
    prepared.subresource_range.baseArrayLayer =
        is_3d_image ? 0u : region.imageSubresource.baseArrayLayer + layer_index;
    prepared.subresource_range.layerCount = 1;
    prepared.regs.format = static_cast<int32_t>(requested_format);
    prepared.regs.width = static_cast<int32_t>(region.imageExtent.width);
    prepared.regs.height = static_cast<int32_t>(region.imageExtent.height);
    prepared.regs.offset = 0;
    prepared.regs.bufferRowLength = static_cast<int32_t>(region.bufferRowLength);
    prepared.regs.offsetX = region.imageOffset.x;
    prepared.regs.offsetY = region.imageOffset.y;
    prepared.regs.reserved0 = 0;
    prepared.groups_x = (region.imageExtent.width + 7u) / 8u;
    prepared.groups_y = (region.imageExtent.height + 7u) / 8u;

    *out_prepared = prepared;
    return true;
}

bool resolve_mapped_buffer_source(
    VkBuffer buffer,
    VkDeviceSize relative_offset,
    VkDeviceSize byte_size,
    const uint8_t** out_ptr,
    size_t* out_size) {
    if (!out_ptr || !out_size || buffer == VK_NULL_HANDLE || byte_size == 0) {
        return false;
    }

    std::shared_lock<std::shared_mutex> guard(g_lock);
    auto binding_it = g_buffer_bindings.find(dispatch_key(buffer));
    if (binding_it == g_buffer_bindings.end()) {
        return false;
    }
    const TrackedBufferBinding& binding = binding_it->second;
    auto map_it = g_memory_maps.find(dispatch_key(binding.memory));
    if (map_it == g_memory_maps.end() || !map_it->second.data) {
        return false;
    }
    const TrackedMemoryMap& map = map_it->second;
    if (binding.memory_offset > std::numeric_limits<VkDeviceSize>::max() - relative_offset) {
        return false;
    }
    const VkDeviceSize absolute_offset = binding.memory_offset + relative_offset;
    if (absolute_offset < map.offset) {
        return false;
    }
    const VkDeviceSize map_relative = absolute_offset - map.offset;
    if (map.size != VK_WHOLE_SIZE) {
        if (map_relative > map.size || byte_size > map.size - map_relative) {
            return false;
        }
    }
    if (map_relative > static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max()) ||
        byte_size > static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max())) {
        return false;
    }

    *out_ptr = static_cast<const uint8_t*>(map.data) + static_cast<size_t>(map_relative);
    *out_size = static_cast<size_t>(byte_size);
    return true;
}

struct CpuDecodedTextureCacheKey {
    VkFormat compressed_format = VK_FORMAT_UNDEFINED;
    VkFormat output_format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t buffer_row_length = 0;
    uint32_t buffer_image_height = 0;
    size_t compressed_size = 0;
    uint64_t compressed_hash = 0;

    bool operator==(const CpuDecodedTextureCacheKey& other) const {
        return compressed_format == other.compressed_format &&
               output_format == other.output_format &&
               width == other.width &&
               height == other.height &&
               buffer_row_length == other.buffer_row_length &&
               buffer_image_height == other.buffer_image_height &&
               compressed_size == other.compressed_size &&
               compressed_hash == other.compressed_hash;
    }
};

struct CpuDecodedTextureCacheEntry {
    CpuDecodedTextureCacheKey key{};
    std::vector<uint8_t> pixels;
    uint64_t last_used = 0;
};

constexpr size_t kCpuDecodedTextureCacheMaxBytes = 64ull * 1024ull * 1024ull;
constexpr size_t kCpuDecodedTextureCacheMaxEntryBytes = 16ull * 1024ull * 1024ull;
std::mutex g_cpu_decoded_texture_cache_mutex;
std::vector<CpuDecodedTextureCacheEntry> g_cpu_decoded_texture_cache;
size_t g_cpu_decoded_texture_cache_bytes = 0;
uint64_t g_cpu_decoded_texture_cache_clock = 0;

uint64_t hash_cpu_decode_source(const uint8_t* data, size_t size) {
    constexpr uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    uint64_t hash = kFnvOffset;
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

bool try_get_cpu_decoded_texture_cache(
    const CpuDecodedTextureCacheKey& key,
    std::vector<uint8_t>* out_pixels) {
    if (!out_pixels) {
        return false;
    }
    std::lock_guard<std::mutex> guard(g_cpu_decoded_texture_cache_mutex);
    ++g_cpu_decoded_texture_cache_clock;
    for (CpuDecodedTextureCacheEntry& entry : g_cpu_decoded_texture_cache) {
        if (entry.key == key) {
            entry.last_used = g_cpu_decoded_texture_cache_clock;
            *out_pixels = entry.pixels;
            return true;
        }
    }
    return false;
}

void prune_cpu_decoded_texture_cache_locked() {
    while (g_cpu_decoded_texture_cache_bytes > kCpuDecodedTextureCacheMaxBytes &&
           !g_cpu_decoded_texture_cache.empty()) {
        auto oldest = std::min_element(
            g_cpu_decoded_texture_cache.begin(),
            g_cpu_decoded_texture_cache.end(),
            [](const CpuDecodedTextureCacheEntry& lhs, const CpuDecodedTextureCacheEntry& rhs) {
                return lhs.last_used < rhs.last_used;
            });
        if (oldest == g_cpu_decoded_texture_cache.end()) {
            break;
        }
        g_cpu_decoded_texture_cache_bytes -= oldest->pixels.size();
        g_cpu_decoded_texture_cache.erase(oldest);
    }
}

void store_cpu_decoded_texture_cache(
    const CpuDecodedTextureCacheKey& key,
    const std::vector<uint8_t>& pixels) {
    if (pixels.empty() || pixels.size() > kCpuDecodedTextureCacheMaxEntryBytes) {
        return;
    }
    std::lock_guard<std::mutex> guard(g_cpu_decoded_texture_cache_mutex);
    ++g_cpu_decoded_texture_cache_clock;
    for (CpuDecodedTextureCacheEntry& entry : g_cpu_decoded_texture_cache) {
        if (entry.key == key) {
            g_cpu_decoded_texture_cache_bytes -= entry.pixels.size();
            entry.pixels = pixels;
            entry.last_used = g_cpu_decoded_texture_cache_clock;
            g_cpu_decoded_texture_cache_bytes += entry.pixels.size();
            prune_cpu_decoded_texture_cache_locked();
            return;
        }
    }

    CpuDecodedTextureCacheEntry entry{};
    entry.key = key;
    entry.pixels = pixels;
    entry.last_used = g_cpu_decoded_texture_cache_clock;
    g_cpu_decoded_texture_cache_bytes += entry.pixels.size();
    g_cpu_decoded_texture_cache.push_back(std::move(entry));
    prune_cpu_decoded_texture_cache_locked();
}

bool try_cpu_decode_copy_regions(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    VmaRuntime* vma_runtime,
    VkBuffer src_buffer,
    VkImage dst_image,
    VkImageLayout dst_layout,
    uint32_t region_count,
    const VkBufferImageCopy* regions,
    const VirtualImageInfo& virtual_info,
    VkFormat decode_format) {
    if (!vma_runtime || vma_runtime->allocator == VK_NULL_HANDLE ||
        !dispatch.cmd_copy_buffer_to_image ||
        !regions ||
        region_count == 0 ||
        dst_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
        dst_layout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        return false;
    }

    const uint32_t texel_size = bcn_cpu_output_texel_size(virtual_info.real_format);
    if (texel_size == 0) {
        return false;
    }
    const LayerSettingsSnapshot settings = snapshot_layer_settings();
    const VkDeviceSize max_cpu_upload_bytes =
        static_cast<VkDeviceSize>(settings.cpu_fallback_max_upload_mb) * 1024ull * 1024ull;

    const bool is_3d_virtual_image = virtual_info.image_type == VK_IMAGE_TYPE_3D;
    std::vector<StagingAllocation> staged_uploads;
    std::vector<VkBufferImageCopy> decoded_regions;
    staged_uploads.reserve(region_count);
    decoded_regions.reserve(region_count);

    for (uint32_t r = 0; r < region_count; ++r) {
        const VkBufferImageCopy& region = regions[r];
        if ((!is_3d_virtual_image && region.imageExtent.depth != 1) ||
            (is_3d_virtual_image &&
             (region.imageOffset.z < 0 ||
              region.imageExtent.depth == 0 ||
              region.imageSubresource.baseArrayLayer != 0 ||
              (region.imageSubresource.layerCount != 0 && region.imageSubresource.layerCount != 1)))) {
            release_staging_allocations(vma_runtime, &staged_uploads);
            return false;
        }
        if (!is_3d_virtual_image && region.imageOffset.z != 0) {
            release_staging_allocations(vma_runtime, &staged_uploads);
            return false;
        }

        const uint32_t layer_count = region.imageSubresource.layerCount ? region.imageSubresource.layerCount : 1u;
        const uint32_t depth_count = is_3d_virtual_image ? region.imageExtent.depth : 1u;
        const uint32_t block_size = block_size_bytes(decode_format);
        const uint32_t blocks_x = (std::max(region.bufferRowLength, region.imageExtent.width) + 3u) / 4u;
        const uint32_t rows = region.bufferImageHeight ? region.bufferImageHeight : region.imageExtent.height;
        const uint32_t blocks_y = (rows + 3u) / 4u;
        const VkDeviceSize slice_stride = static_cast<VkDeviceSize>(blocks_x) *
                                          static_cast<VkDeviceSize>(blocks_y) *
                                          static_cast<VkDeviceSize>(block_size);
        const VkDeviceSize layer_stride = slice_stride * static_cast<VkDeviceSize>(depth_count);
        const uint32_t copy_blocks_x = (region.imageExtent.width + 3u) / 4u;
        const uint32_t copy_blocks_y = (region.imageExtent.height + 3u) / 4u;
        const VkDeviceSize row_pitch = static_cast<VkDeviceSize>(blocks_x) * block_size;
        const VkDeviceSize active_row_bytes = static_cast<VkDeviceSize>(copy_blocks_x) * block_size;
        const VkDeviceSize copy_footprint =
            static_cast<VkDeviceSize>(copy_blocks_y - 1u) * row_pitch + active_row_bytes;
        if (slice_stride == 0 || copy_footprint == 0 || copy_footprint > slice_stride) {
            release_staging_allocations(vma_runtime, &staged_uploads);
            return false;
        }

        for (uint32_t layer = 0; layer < layer_count; ++layer) {
            for (uint32_t depth_slice = 0; depth_slice < depth_count; ++depth_slice) {
                const VkDeviceSize src_offset = region.bufferOffset +
                    static_cast<VkDeviceSize>(layer) * layer_stride +
                    static_cast<VkDeviceSize>(depth_slice) * slice_stride;
                const uint8_t* mapped_src = nullptr;
                size_t mapped_size = 0;
                if (!resolve_mapped_buffer_source(src_buffer, src_offset, copy_footprint, &mapped_src, &mapped_size)) {
                    release_staging_allocations(vma_runtime, &staged_uploads);
                    return false;
                }

                BcnCpuDecodeRegion cpu_region{};
                cpu_region.compressed_format = decode_format;
                cpu_region.output_format = virtual_info.real_format;
                cpu_region.width = region.imageExtent.width;
                cpu_region.height = region.imageExtent.height;
                cpu_region.buffer_row_length = region.bufferRowLength;
                cpu_region.buffer_image_height = region.bufferImageHeight;
                cpu_region.buffer_offset = 0;

                const VkDeviceSize decoded_size =
                    static_cast<VkDeviceSize>(cpu_region.width) *
                    static_cast<VkDeviceSize>(cpu_region.height) *
                    static_cast<VkDeviceSize>(texel_size);
                if (decoded_size == 0 || decoded_size > max_cpu_upload_bytes) {
                    EXYNOS_LOGW(
                        "Skipping BCn CPU decode region: decoded upload too large (%llu bytes, limit=%llu).",
                        static_cast<unsigned long long>(decoded_size),
                        static_cast<unsigned long long>(max_cpu_upload_bytes));
                    release_staging_allocations(vma_runtime, &staged_uploads);
                    return false;
                }

                std::vector<uint8_t> decoded_pixels;
                CpuDecodedTextureCacheKey cache_key{};
                cache_key.compressed_format = cpu_region.compressed_format;
                cache_key.output_format = cpu_region.output_format;
                cache_key.width = cpu_region.width;
                cache_key.height = cpu_region.height;
                cache_key.buffer_row_length = cpu_region.buffer_row_length;
                cache_key.buffer_image_height = cpu_region.buffer_image_height;
                cache_key.compressed_size = mapped_size;
                cache_key.compressed_hash = hash_cpu_decode_source(mapped_src, mapped_size);
                if (!try_get_cpu_decoded_texture_cache(cache_key, &decoded_pixels)) {
                    if (!bcn_cpu_decode_region(mapped_src, mapped_size, cpu_region, &decoded_pixels) ||
                        decoded_pixels.empty()) {
                        release_staging_allocations(vma_runtime, &staged_uploads);
                        return false;
                    }
                    store_cpu_decoded_texture_cache(cache_key, decoded_pixels);
                }

                StagingAllocation upload{};
                if (!create_cpu_upload_staging_for_region(
                        vma_runtime,
                        static_cast<VkDeviceSize>(decoded_pixels.size()),
                        decoded_pixels.data(),
                        &upload)) {
                    release_staging_allocations(vma_runtime, &staged_uploads);
                    return false;
                }

                VkBufferImageCopy decoded_region{};
                decoded_region.bufferOffset = upload.offset;
                decoded_region.bufferRowLength = 0;
                decoded_region.bufferImageHeight = 0;
                decoded_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                decoded_region.imageSubresource.mipLevel = region.imageSubresource.mipLevel;
                decoded_region.imageSubresource.baseArrayLayer =
                    is_3d_virtual_image ? 0u : region.imageSubresource.baseArrayLayer + layer;
                decoded_region.imageSubresource.layerCount = 1;
                decoded_region.imageOffset = region.imageOffset;
                if (is_3d_virtual_image) {
                    decoded_region.imageOffset.z += static_cast<int32_t>(depth_slice);
                }
                decoded_region.imageExtent = {region.imageExtent.width, region.imageExtent.height, 1};

                staged_uploads.push_back(upload);
                decoded_regions.push_back(decoded_region);
            }
        }
    }

    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.srcAccessMask = access_mask_for_layout(dst_layout);
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_transfer.oldLayout = dst_layout;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = dst_image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.baseMipLevel = 0;
    to_transfer.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    to_transfer.subresourceRange.baseArrayLayer = 0;
    to_transfer.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    submit_pipeline_barrier_image(
        dispatch,
        command_buffer,
        stage_mask_for_layout(dst_layout),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        1,
        &to_transfer);

    for (size_t i = 0; i < staged_uploads.size(); ++i) {
        dispatch.cmd_copy_buffer_to_image(
            command_buffer,
            staged_uploads[i].buffer,
            dst_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &decoded_regions[i]);
    }

    VkImageMemoryBarrier from_transfer{};
    from_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    from_transfer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    from_transfer.dstAccessMask = access_mask_for_layout(dst_layout);
    if (from_transfer.dstAccessMask == 0) {
        from_transfer.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }
    from_transfer.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    from_transfer.newLayout = dst_layout;
    from_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    from_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    from_transfer.image = dst_image;
    from_transfer.subresourceRange = to_transfer.subresourceRange;
    submit_pipeline_barrier_image(
        dispatch,
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        stage_mask_for_layout(dst_layout),
        1,
        &from_transfer);

    for (StagingAllocation& upload : staged_uploads) {
        track_command_buffer_staging_allocation(command_buffer, std::move(upload));
    }
    staged_uploads.clear();
    EXYNOS_LOGI(
        "BCn CPU decode uploaded %llu region(s) for image %p (format=%d real=%d).",
        static_cast<unsigned long long>(decoded_regions.size()),
        static_cast<void*>(dst_image),
        static_cast<int>(decode_format),
        static_cast<int>(virtual_info.real_format));
    return true;
}

bool build_special_copy_region_plan(
    const VkImageCopy& region,
    VkPipeline pipeline,
    DecoderShaderKind shader_kind,
    uint32_t layer_index,
    PreparedSpecialCopyRegion* out_prepared) {
    if (!out_prepared) {
        return false;
    }
    if (region.extent.depth != 1 ||
        region.srcOffset.z != 0 ||
        region.dstOffset.z != 0 ||
        region.srcSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        region.dstSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) {
        return false;
    }
    if (region.extent.width > static_cast<uint32_t>(INT32_MAX) ||
        region.extent.height > static_cast<uint32_t>(INT32_MAX) ||
        region.srcOffset.x < 0 ||
        region.srcOffset.y < 0 ||
        region.dstOffset.x < 0 ||
        region.dstOffset.y < 0) {
        EXYNOS_LOGW("CopyImage region exceeds push constant integer range.");
        return false;
    }

    PreparedSpecialCopyRegion prepared{};
    prepared.shader_kind = shader_kind;
    prepared.pipeline = pipeline;
    prepared.src_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prepared.src_subresource_range.baseMipLevel = region.srcSubresource.mipLevel;
    prepared.src_subresource_range.levelCount = 1;
    prepared.src_subresource_range.baseArrayLayer = region.srcSubresource.baseArrayLayer + layer_index;
    prepared.src_subresource_range.layerCount = 1;
    prepared.dst_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prepared.dst_subresource_range.baseMipLevel = region.dstSubresource.mipLevel;
    prepared.dst_subresource_range.levelCount = 1;
    prepared.dst_subresource_range.baseArrayLayer = region.dstSubresource.baseArrayLayer + layer_index;
    prepared.dst_subresource_range.layerCount = 1;
    prepared.regs.srcOffsetX = region.srcOffset.x;
    prepared.regs.srcOffsetY = region.srcOffset.y;
    prepared.regs.dstOffsetX = region.dstOffset.x;
    prepared.regs.dstOffsetY = region.dstOffset.y;
    prepared.regs.width = static_cast<int32_t>(region.extent.width);
    prepared.regs.height = static_cast<int32_t>(region.extent.height);
    prepared.regs.reserved0 = 0;
    prepared.regs.reserved1 = 0;
    prepared.groups_x = (region.extent.width + 7u) / 8u;
    prepared.groups_y = (region.extent.height + 7u) / 8u;

    *out_prepared = prepared;
    return true;
}

std::shared_ptr<ComputeRuntime> get_or_create_compute_runtime(void* device_key) {
    {
        std::shared_lock<std::shared_mutex> read_guard(g_lock);
        auto it = g_compute_runtime.find(device_key);
        if (it != g_compute_runtime.end()) {
            return it->second;
        }
    }

    std::lock_guard<std::shared_mutex> write_guard(g_lock);
    auto it = g_compute_runtime.find(device_key);
    if (it != g_compute_runtime.end()) {
        return it->second;
    }
    auto runtime = std::make_shared<ComputeRuntime>();
    g_compute_runtime[device_key] = runtime;
    return runtime;
}

void prewarm_compute_runtime_if_needed(
    VkDevice device,
    const DeviceDispatch& dispatch,
    bool is_xclipse_device) {
    if (device == VK_NULL_HANDLE || !is_xclipse_device) {
        return;
    }

    auto runtime = get_or_create_compute_runtime(dispatch_key(device));
    std::lock_guard<std::mutex> init_guard(runtime->init_mutex);
    if (runtime->initialized) {
        return;
    }
    if (!initialize_compute_runtime(
            device,
            dispatch,
            compute_runtime_config_for_device(device),
            runtime.get())) {
        EXYNOS_LOGW(
            "Compute runtime prewarm failed during device creation. "
            "The layer will retry lazily on first BCn decode/copy use.");
    }
}

std::shared_ptr<VmaRuntime> get_or_create_vma_runtime(void* device_key) {
    {
        std::shared_lock<std::shared_mutex> read_guard(g_lock);
        auto it = g_vma_runtime.find(device_key);
        if (it != g_vma_runtime.end()) {
            return it->second;
        }
    }

    std::lock_guard<std::shared_mutex> write_guard(g_lock);
    auto it = g_vma_runtime.find(device_key);
    if (it != g_vma_runtime.end()) {
        return it->second;
    }
    auto runtime = std::make_shared<VmaRuntime>();
    g_vma_runtime[device_key] = runtime;
    return runtime;
}

void release_command_buffer_resources(
    VkDevice device,
    VkCommandBuffer command_buffer,
    const DeviceDispatch& dispatch) {
    if (device == VK_NULL_HANDLE || command_buffer == VK_NULL_HANDLE) {
        return;
    }

    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    std::shared_ptr<VmaRuntime> vma_runtime;
    std::shared_ptr<ComputeRuntime> compute_runtime;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto vma_it = g_vma_runtime.find(dispatch_key(device));
        if (vma_it != g_vma_runtime.end() && vma_it->second) {
            vma_runtime = vma_it->second;
        }
        auto compute_it = g_compute_runtime.find(dispatch_key(device));
        if (compute_it != g_compute_runtime.end() && compute_it->second) {
            compute_runtime = compute_it->second;
        }

        void* cb_key = dispatch_key(command_buffer);
        take_command_buffer_staging_allocations(cb_key, &staging_allocations_to_release);
        take_command_buffer_descriptor_sets(cb_key, &descriptor_sets_to_release);
    }

    release_staging_allocations(vma_runtime.get(), &staging_allocations_to_release);
    release_descriptor_sets(device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
    collect_gpu_microbenchmarks(device, dispatch, false);
}

void release_prepared_decode_regions(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaRuntime* vma_runtime,
    std::vector<PreparedDecodeRegion>* prepared_regions) {
    if (!prepared_regions) {
        return;
    }

    std::vector<StagingAllocation> staging_allocations;
    std::vector<TrackedDescriptorSet> descriptor_sets;
    staging_allocations.reserve(prepared_regions->size());
    descriptor_sets.reserve(prepared_regions->size());

    for (PreparedDecodeRegion& prepared : *prepared_regions) {
        if (prepared.staging.buffer != VK_NULL_HANDLE &&
            prepared.staging.allocation != VK_NULL_HANDLE) {
            staging_allocations.push_back(prepared.staging);
            prepared.staging = {};
        }
        if (prepared.descriptor_pool != VK_NULL_HANDLE &&
            prepared.descriptor_set != VK_NULL_HANDLE) {
            descriptor_sets.push_back(
                TrackedDescriptorSet{prepared.descriptor_pool, prepared.descriptor_set});
            prepared.descriptor_pool = VK_NULL_HANDLE;
            prepared.descriptor_set = VK_NULL_HANDLE;
        }
    }

    release_staging_allocations(vma_runtime, &staging_allocations);
    release_descriptor_sets(device, dispatch, runtime, &descriptor_sets);
    prepared_regions->clear();
}

void release_prepared_special_copy_regions(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    std::vector<PreparedSpecialCopyRegion>* prepared_regions) {
    if (!prepared_regions) {
        return;
    }

    std::vector<TrackedDescriptorSet> descriptor_sets;
    descriptor_sets.reserve(prepared_regions->size());
    for (PreparedSpecialCopyRegion& prepared : *prepared_regions) {
        if (prepared.descriptor_pool != VK_NULL_HANDLE &&
            prepared.descriptor_set != VK_NULL_HANDLE) {
            descriptor_sets.push_back(
                TrackedDescriptorSet{prepared.descriptor_pool, prepared.descriptor_set});
            prepared.descriptor_pool = VK_NULL_HANDLE;
            prepared.descriptor_set = VK_NULL_HANDLE;
        }
    }

    release_descriptor_sets(device, dispatch, runtime, &descriptor_sets);
    prepared_regions->clear();
}

void record_special_copy_region(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VkImage src_image,
    VkImageLayout src_layout,
    VkImage dst_image,
    VkImageLayout dst_layout,
    PreparedSpecialCopyRegion* prepared) {
    if (!runtime || !runtime->available || !prepared) {
        return;
    }
    if (prepared->pipeline == VK_NULL_HANDLE ||
        runtime->copy_sampler == VK_NULL_HANDLE ||
        prepared->src_view == VK_NULL_HANDLE ||
        prepared->dst_view == VK_NULL_HANDLE ||
        prepared->descriptor_set == VK_NULL_HANDLE ||
        (!runtime->use_descriptor_buffer && prepared->descriptor_pool == VK_NULL_HANDLE)) {
        EXYNOS_LOGW("Prepared special image copy region is incomplete; skipping command recording.");
        return;
    }

    VkImageMemoryBarrier src_to_sampled{};
    src_to_sampled.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_to_sampled.srcAccessMask = access_mask_for_layout(src_layout);
    src_to_sampled.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src_to_sampled.oldLayout = src_layout;
    src_to_sampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_to_sampled.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_sampled.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_sampled.image = src_image;
    src_to_sampled.subresourceRange = prepared->src_subresource_range;

    VkImageMemoryBarrier dst_to_general{};
    dst_to_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_to_general.srcAccessMask = access_mask_for_layout(dst_layout);
    dst_to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dst_to_general.oldLayout = dst_layout;
    dst_to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    dst_to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_general.image = dst_image;
    dst_to_general.subresourceRange = prepared->dst_subresource_range;

    VkImageMemoryBarrier to_compute[2]{src_to_sampled, dst_to_general};
    submit_pipeline_barrier_image(
        dispatch,
        command_buffer,
        stage_mask_for_layout(src_layout) | stage_mask_for_layout(dst_layout),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        2,
        to_compute);

    dispatch.cmd_bind_pipeline(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        prepared->pipeline);
    if (runtime->use_descriptor_buffer &&
        dispatch.cmd_bind_descriptor_buffers_ext &&
        dispatch.cmd_set_descriptor_buffer_offsets_ext) {
        VkDescriptorBufferBindingInfoEXT binding_info{};
        binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        binding_info.address = runtime->descriptor_buffer_address;
        binding_info.usage =
            VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
            VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
        dispatch.cmd_bind_descriptor_buffers_ext(command_buffer, 1, &binding_info);

        const uint32_t buffer_index = 0;
        const VkDeviceSize descriptor_offset =
            static_cast<VkDeviceSize>(reinterpret_cast<uintptr_t>(prepared->descriptor_set) - 1u);
        dispatch.cmd_set_descriptor_buffer_offsets_ext(
            command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            runtime->pipeline_layout,
            0,
            1,
            &buffer_index,
            &descriptor_offset);
    } else {
        dispatch.cmd_bind_descriptor_sets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            runtime->pipeline_layout,
            0,
            1,
            &prepared->descriptor_set,
            0,
            nullptr);
    }
    dispatch.cmd_push_constants(
        command_buffer,
        runtime->pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(CopyImagePushConstants),
        &prepared->regs);
    dispatch.cmd_dispatch(command_buffer, prepared->groups_x, prepared->groups_y, 1);

    VkImageMemoryBarrier src_restore{};
    src_restore.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_restore.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src_restore.dstAccessMask = access_mask_for_layout(src_layout);
    if (src_restore.dstAccessMask == 0) {
        src_restore.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }
    src_restore.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_restore.newLayout = src_layout;
    src_restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_restore.image = src_image;
    src_restore.subresourceRange = prepared->src_subresource_range;

    VkImageMemoryBarrier dst_restore{};
    dst_restore.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_restore.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dst_restore.dstAccessMask = access_mask_for_layout(dst_layout);
    if (dst_restore.dstAccessMask == 0) {
        dst_restore.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }
    dst_restore.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    dst_restore.newLayout = dst_layout;
    dst_restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_restore.image = dst_image;
    dst_restore.subresourceRange = prepared->dst_subresource_range;

    VkImageMemoryBarrier from_compute[2]{src_restore, dst_restore};
    submit_pipeline_barrier_image(
        dispatch,
        command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        stage_mask_for_layout(src_layout) | stage_mask_for_layout(dst_layout),
        2,
        from_compute);

    if (!runtime->use_descriptor_buffer) {
        track_command_buffer_descriptor_set(command_buffer, prepared->descriptor_pool, prepared->descriptor_set);
        prepared->descriptor_pool = VK_NULL_HANDLE;
        prepared->descriptor_set = VK_NULL_HANDLE;
    }
}

void record_decode_region(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VkImage dst_image,
    VkImageLayout dst_layout,
    VkBuffer src_buffer,
    PreparedDecodeRegion* prepared) {
    if (!runtime || !runtime->available || !prepared) {
        return;
    }
    if (prepared->pipeline == VK_NULL_HANDLE ||
        prepared->storage_view == VK_NULL_HANDLE ||
        prepared->descriptor_set == VK_NULL_HANDLE ||
        (!runtime->use_descriptor_buffer && prepared->descriptor_pool == VK_NULL_HANDLE) ||
        prepared->staging.buffer == VK_NULL_HANDLE ||
        prepared->staging.allocation == VK_NULL_HANDLE ||
        src_buffer == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Prepared decode region is incomplete; skipping command recording.");
        return;
    }

    VkBufferCopy copy_region{};
    copy_region.srcOffset = prepared->src_offset;
    copy_region.dstOffset = prepared->staging.offset;
    copy_region.size = prepared->byte_size;
    dispatch.cmd_copy_buffer(command_buffer, src_buffer, prepared->staging.buffer, 1, &copy_region);

    VkBufferMemoryBarrier buffer_barrier{};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = prepared->staging.buffer;
    buffer_barrier.offset = prepared->staging.offset;
    buffer_barrier.size = prepared->byte_size;
    submit_pipeline_barrier_buffer(
        dispatch,
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        1,
        &buffer_barrier);

    VkImageMemoryBarrier to_general{};
    to_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_general.srcAccessMask = access_mask_for_layout(dst_layout);
    to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_general.oldLayout = dst_layout;
    to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.image = dst_image;
    to_general.subresourceRange = prepared->subresource_range;
    submit_pipeline_barrier_image(
        dispatch,
        command_buffer,
        stage_mask_for_layout(dst_layout),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        1,
        &to_general);

    dispatch.cmd_bind_pipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, prepared->pipeline);
    if (runtime->use_descriptor_buffer &&
        dispatch.cmd_bind_descriptor_buffers_ext &&
        dispatch.cmd_set_descriptor_buffer_offsets_ext) {
        VkDescriptorBufferBindingInfoEXT binding_info{};
        binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        binding_info.address = runtime->descriptor_buffer_address;
        binding_info.usage =
            VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
            VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
        dispatch.cmd_bind_descriptor_buffers_ext(command_buffer, 1, &binding_info);

        const uint32_t buffer_index = 0;
        const VkDeviceSize descriptor_offset =
            static_cast<VkDeviceSize>(reinterpret_cast<uintptr_t>(prepared->descriptor_set) - 1u);
        dispatch.cmd_set_descriptor_buffer_offsets_ext(
            command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            runtime->pipeline_layout,
            0,
            1,
            &buffer_index,
            &descriptor_offset);
    } else {
        dispatch.cmd_bind_descriptor_sets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            runtime->pipeline_layout,
            0,
            1,
            &prepared->descriptor_set,
            0,
            nullptr);
    }
    dispatch.cmd_push_constants(
        command_buffer,
        runtime->pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(DecodePushConstants),
        &prepared->regs);

    dispatch.cmd_dispatch(command_buffer, prepared->groups_x, prepared->groups_y, 1);

    VkImageMemoryBarrier from_general{};
    from_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    from_general.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    from_general.dstAccessMask = access_mask_for_layout(dst_layout);
    if (from_general.dstAccessMask == 0) {
        from_general.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }
    from_general.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    from_general.newLayout = dst_layout;
    from_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    from_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    from_general.image = dst_image;
    from_general.subresourceRange = prepared->subresource_range;
    submit_pipeline_barrier_image(
        dispatch,
        command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        stage_mask_for_layout(dst_layout),
        1,
        &from_general);

    track_command_buffer_staging_allocation(command_buffer, std::move(prepared->staging));
    prepared->staging = {};
    if (!runtime->use_descriptor_buffer) {
        track_command_buffer_descriptor_set(command_buffer, prepared->descriptor_pool, prepared->descriptor_set);
        prepared->descriptor_pool = VK_NULL_HANDLE;
        prepared->descriptor_set = VK_NULL_HANDLE;
    }
}

bool try_decode_copy_regions(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkBuffer src_buffer,
    VkImage dst_image,
    VkImageLayout dst_layout,
    uint32_t region_count,
    const VkBufferImageCopy* regions) {
    if (!regions || region_count == 0) {
        return false;
    }
    if (device == VK_NULL_HANDLE || src_buffer == VK_NULL_HANDLE) {
        return false;
    }

    const bool microbenchmark_enabled = snapshot_layer_settings().microbenchmark_enabled;
    const auto benchmark_start = std::chrono::steady_clock::now();
    auto finish_benchmark = [&](DecoderShaderKind shader_kind, uint32_t work_items, bool success) {
        if (!microbenchmark_enabled) {
            return success;
        }
        const auto duration_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - benchmark_start)
                .count());
        record_microbenchmark_sample(
            BenchmarkDomain::DecodeCpu,
            shader_kind,
            duration_ns,
            work_items,
            success);
        return success;
    };

    g_decode_attempts.fetch_add(1);
    void* image_key = dispatch_key(dst_image);
    VirtualImageInfo virtual_info{};
    bool is_virtual_image = false;
    DecodeImageState image_state{};
    bool has_image_state = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_virtual_images.find(image_key);
        if (it != g_virtual_images.end()) {
            virtual_info = it->second;
            is_virtual_image = true;
        }
        auto it_state = g_decode_image_state.find(image_key);
        if (it_state != g_decode_image_state.end()) {
            image_state = it_state->second;
            has_image_state = true;
        }
    }
    if (!is_virtual_image) {
        maybe_log_decode_stats();
        return finish_benchmark(DecoderShaderKind::None, 0, false);
    }
    const VkFormat decode_format =
        virtual_info.decode_format != VK_FORMAT_UNDEFINED
            ? virtual_info.decode_format
            : virtual_info.requested_format;

    auto mark_image_blocked = [&]() {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto& state = g_decode_image_state[image_key];
        state.blocked_passthrough = true;
        state.blocked_copy_count = 0;
        state.failure_count += 1;
    };

    if (has_image_state && image_state.blocked_passthrough) {
        uint32_t next_block_count = image_state.blocked_copy_count + 1u;
        if (next_block_count < kDecodeBlockedRetryInterval) {
            {
                std::lock_guard<std::shared_mutex> guard(g_lock);
                auto& state = g_decode_image_state[image_key];
                state.blocked_passthrough = true;
                state.blocked_copy_count = next_block_count;
            }
            g_decode_blocked_copies.fetch_add(1);
            maybe_log_decode_stats();
            return finish_benchmark(shader_kind_for_decode(
                decode_format,
                virtual_info.real_format), 0, false);
        }
        {
            std::lock_guard<std::shared_mutex> guard(g_lock);
            auto& state = g_decode_image_state[image_key];
            state.blocked_passthrough = false;
            state.blocked_copy_count = 0;
        }
        g_decode_retry_attempts.fetch_add(1);
        EXYNOS_LOGI(
            "Retrying BCn decode for blocked image %p after %u blocked copies.",
            static_cast<void*>(dst_image),
            kDecodeBlockedRetryInterval);
    }

    DecoderShaderKind shader_kind = shader_kind_for_decode(decode_format, virtual_info.real_format);
    if (is_bcn_srgb_format(decode_format)) {
        g_decode_srgb_paths.fetch_add(1);
    }

    auto vma_runtime = get_or_create_vma_runtime(dispatch_key(device));
    {
        std::lock_guard<std::mutex> init_guard(vma_runtime->init_mutex);
        VmaRuntimeInitInputs vma_inputs{};
        if (!gather_vma_runtime_init_inputs(dispatch_key(device), &vma_inputs)) {
            EXYNOS_LOGW("VMA init failed: device missing instance/physical/dispatch mapping.");
            mark_image_blocked();
            g_decode_failures.fetch_add(1);
            g_decode_passthrough_activations.fetch_add(1);
            g_decode_blocked_copies.fetch_add(1);
            maybe_log_decode_stats();
            return finish_benchmark(shader_kind, 0, false);
        }
        if (!initialize_vma_runtime(
                vma_inputs.instance,
                vma_inputs.physical_device,
                device,
                vma_inputs.instance_dispatch,
                dispatch,
                vma_runtime.get())) {
            EXYNOS_LOGW("VMA runtime unavailable for BCn decode path. Marking image as blocked passthrough.");
            mark_image_blocked();
            g_decode_failures.fetch_add(1);
            g_decode_passthrough_activations.fetch_add(1);
            g_decode_blocked_copies.fetch_add(1);
            maybe_log_decode_stats();
            return finish_benchmark(shader_kind, 0, false);
        }
    }
    if (vma_runtime->allocator == VK_NULL_HANDLE) {
        EXYNOS_LOGW("VMA allocator is null for BCn decode path. Marking image as blocked passthrough.");
        mark_image_blocked();
        g_decode_failures.fetch_add(1);
        g_decode_passthrough_activations.fetch_add(1);
        g_decode_blocked_copies.fetch_add(1);
        maybe_log_decode_stats();
        return finish_benchmark(shader_kind, 0, false);
    }

    auto try_cpu_fallback = [&]() {
        bool success = try_cpu_decode_copy_regions(
            command_buffer,
            device,
            dispatch,
            vma_runtime.get(),
            src_buffer,
            dst_image,
            dst_layout,
            region_count,
            regions,
            virtual_info,
            decode_format);
        if (success) {
            g_decode_successes.fetch_add(1);
            maybe_log_decode_stats();
            return finish_benchmark(shader_kind, region_count, true);
        }
        return finish_benchmark(shader_kind, 0, false);
    };

    if (try_cpu_fallback()) {
        return true;
    }

    EXYNOS_LOGW("BCn CPU decode unavailable for virtual image. Marking image as blocked passthrough.");
    mark_image_blocked();
    g_decode_failures.fetch_add(1);
    g_decode_passthrough_activations.fetch_add(1);
    g_decode_blocked_copies.fetch_add(1);
    maybe_log_decode_stats();
    return finish_benchmark(shader_kind, 0, false);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    refresh_layer_settings(pCreateInfo);

    auto* chain_info = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));
    while (chain_info &&
           (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
            chain_info->function != VK_LAYER_LINK_INFO)) {
        chain_info = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain_info->pNext));
    }
    if (!chain_info || !chain_info->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr next_gipa = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance next_create_instance =
        reinterpret_cast<PFN_vkCreateInstance>(next_gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    if (!next_create_instance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = next_create_instance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS || !pInstance || *pInstance == VK_NULL_HANDLE) {
        return result;
    }

    InstanceDispatch dispatch{};
    dispatch.get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        next_gipa(*pInstance, "vkGetInstanceProcAddr"));
    dispatch.destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
        next_gipa(*pInstance, "vkDestroyInstance"));
    dispatch.create_device = reinterpret_cast<PFN_vkCreateDevice>(
        next_gipa(*pInstance, "vkCreateDevice"));
    dispatch.enumerate_physical_devices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        next_gipa(*pInstance, "vkEnumeratePhysicalDevices"));
    dispatch.get_physical_device_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceProperties"));
    dispatch.get_physical_device_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceProperties2"));
    dispatch.get_physical_device_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFeatures"));
    dispatch.get_physical_device_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFeatures2"));
    dispatch.get_physical_device_format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties"));
    dispatch.get_physical_device_image_format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceImageFormatProperties"));
    dispatch.get_physical_device_format_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties2"));
    dispatch.get_physical_device_image_format_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceImageFormatProperties2"));
#ifdef VK_KHR_get_physical_device_properties2
    dispatch.get_physical_device_properties2_khr = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceProperties2KHR"));
    dispatch.get_physical_device_features2_khr = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2KHR>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFeatures2KHR"));
    dispatch.get_physical_device_format_properties2_khr = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2KHR>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties2KHR"));
    dispatch.get_physical_device_image_format_properties2_khr = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2KHR>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceImageFormatProperties2KHR"));
#endif

    InstanceRuntime instance_runtime = make_instance_runtime(pCreateInfo);
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_instance_dispatch[dispatch_key(*pInstance)] = dispatch;
        g_instance_runtime[dispatch_key(*pInstance)] = instance_runtime;
    }
    EXYNOS_LOGI(
        "Application detected: app='%s' engine='%s' appVersion=%u engineVersion=%u api=0x%x dxvk=%d vkd3d=%d clvk=%d.",
        instance_runtime.application_name.c_str(),
        instance_runtime.engine_name.c_str(),
        instance_runtime.application_version,
        instance_runtime.engine_version,
        instance_runtime.api_version,
        static_cast<int>(instance_runtime.is_dxvk),
        static_cast<int>(instance_runtime.is_vkd3d_proton),
        static_cast<int>(instance_runtime.is_clvk));
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator) {
    InstanceDispatch dispatch{};
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto it = g_instance_dispatch.find(dispatch_key(instance));
        if (it == g_instance_dispatch.end()) {
            return;
        }
        dispatch = it->second;
        g_instance_dispatch.erase(it);
        g_instance_runtime.erase(dispatch_key(instance));
        for (auto phys_it = g_physical_to_instance.begin(); phys_it != g_physical_to_instance.end();) {
            if (phys_it->second == dispatch_key(instance)) {
                for (auto cache_it = g_bcn_native_support_cache.begin(); cache_it != g_bcn_native_support_cache.end();) {
                    if (cache_it->first.physical == phys_it->first) {
                        cache_it = g_bcn_native_support_cache.erase(cache_it);
                    } else {
                        ++cache_it;
                    }
                }
                g_physical_to_instance_handle.erase(phys_it->first);
                g_physical_runtime.erase(phys_it->first);
                phys_it = g_physical_to_instance.erase(phys_it);
            } else {
                ++phys_it;
            }
        }
    }

    if (dispatch.destroy_instance) {
        dispatch.destroy_instance(instance, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_EnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {
    InstanceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_instance_dispatch.find(dispatch_key(instance));
        if (it == g_instance_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }

    if (!dispatch.enumerate_physical_devices) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.enumerate_physical_devices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    if (result == VK_SUCCESS && pPhysicalDevices && pPhysicalDeviceCount) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
            auto phys_key = dispatch_key(pPhysicalDevices[i]);
            g_physical_to_instance[phys_key] = dispatch_key(instance);
            g_physical_to_instance_handle[phys_key] = instance;
            PhysicalRuntime phys_runtime{};
            if (dispatch.get_physical_device_properties) {
                VkPhysicalDeviceProperties props{};
                dispatch.get_physical_device_properties(pPhysicalDevices[i], &props);
                phys_runtime.vendor_id = props.vendorID;
                phys_runtime.is_xclipse = (props.vendorID == 0x144D) || (std::strstr(props.deviceName, "Xclipse") != nullptr);
            }
            phys_runtime.driver_id = query_physical_driver_id(pPhysicalDevices[i], dispatch);
            g_physical_runtime[phys_key] = phys_runtime;
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties* pFormatProperties) {
    if (!pFormatProperties) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !dispatch.get_physical_device_format_properties) {
        std::memset(pFormatProperties, 0, sizeof(*pFormatProperties));
        return;
    }

    dispatch.get_physical_device_format_properties(physicalDevice, format, pFormatProperties);
    virtualize_format_properties_if_needed(physicalDevice, format, pFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2* pFormatProperties) {
    if (!pFormatProperties) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr)) {
        std::memset(&pFormatProperties->formatProperties, 0, sizeof(pFormatProperties->formatProperties));
        return;
    }

    if (dispatch.get_physical_device_format_properties2) {
        dispatch.get_physical_device_format_properties2(physicalDevice, format, pFormatProperties);
    } else if (dispatch.get_physical_device_format_properties) {
        dispatch.get_physical_device_format_properties(physicalDevice, format, &pFormatProperties->formatProperties);
    } else {
        std::memset(&pFormatProperties->formatProperties, 0, sizeof(pFormatProperties->formatProperties));
    }

    virtualize_format_properties_if_needed(physicalDevice, format, &pFormatProperties->formatProperties);
}

#ifdef VK_KHR_get_physical_device_properties2
VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2KHR* pFormatProperties) {
    if (!pFormatProperties) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr)) {
        std::memset(&pFormatProperties->formatProperties, 0, sizeof(pFormatProperties->formatProperties));
        return;
    }

    if (dispatch.get_physical_device_format_properties2_khr) {
        dispatch.get_physical_device_format_properties2_khr(physicalDevice, format, pFormatProperties);
    } else if (dispatch.get_physical_device_format_properties2) {
        dispatch.get_physical_device_format_properties2(
            physicalDevice,
            format,
            reinterpret_cast<VkFormatProperties2*>(pFormatProperties));
    } else if (dispatch.get_physical_device_format_properties) {
        dispatch.get_physical_device_format_properties(physicalDevice, format, &pFormatProperties->formatProperties);
    } else {
        std::memset(&pFormatProperties->formatProperties, 0, sizeof(pFormatProperties->formatProperties));
    }

    virtualize_format_properties_if_needed(physicalDevice, format, &pFormatProperties->formatProperties);
}
#endif

constexpr VkImageUsageFlags kBcnVirtualImageInternalUsage =
    VK_IMAGE_USAGE_STORAGE_BIT |
    VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

constexpr VkImageCreateFlags kBcnVirtualUnsupportedImageFlags =
    VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
    VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
    VK_IMAGE_CREATE_DISJOINT_BIT;

struct VirtualBcnImageFormatPnextPatch {
    void* cloned_pnext = nullptr;
    std::vector<VkFormat> patched_view_formats;
};

inline void zero_image_format_properties(VkImageFormatProperties* props) {
    if (props) {
        *props = VkImageFormatProperties{};
    }
}

inline uint32_t max3_u32(uint32_t a, uint32_t b, uint32_t c) {
    return std::max(a, std::max(b, c));
}

inline uint32_t mip_levels_for_extent(VkExtent3D extent) {
    uint32_t dim = max3_u32(extent.width, extent.height, extent.depth);
    uint32_t levels = 0;
    while (dim > 0) {
        ++levels;
        dim >>= 1;
    }
    return levels;
}

bool is_depth_stencil_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_S8_UINT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

bool format_has_stencil_aspect(VkFormat format) {
    switch (format) {
        case VK_FORMAT_S8_UINT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

struct DepthFormatSupportKey {
    void* physical = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;

    bool operator==(const DepthFormatSupportKey& other) const {
        return physical == other.physical &&
               format == other.format &&
               type == other.type &&
               tiling == other.tiling &&
               usage == other.usage &&
               flags == other.flags;
    }
};

struct DepthFormatSupportKeyHash {
    size_t operator()(const DepthFormatSupportKey& key) const {
        size_t h = reinterpret_cast<size_t>(key.physical);
        h ^= static_cast<size_t>(static_cast<uint32_t>(key.format) + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(static_cast<uint32_t>(key.type) + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(static_cast<uint32_t>(key.tiling) + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(key.usage + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(key.flags + 0x9e3779b9u + (h << 6) + (h >> 2));
        return h;
    }
};

std::mutex g_depth_format_support_cache_mutex;
std::unordered_map<DepthFormatSupportKey, bool, DepthFormatSupportKeyHash> g_depth_format_support_cache;

bool supports_image_format_for_create(
    VkPhysicalDevice physical_device,
    const InstanceDispatch& dispatch,
    VkFormat format,
    const VkImageCreateInfo& create_info) {
    if (physical_device == VK_NULL_HANDLE || format == VK_FORMAT_UNDEFINED) {
        return false;
    }
    DepthFormatSupportKey key{};
    key.physical = dispatch_key(physical_device);
    key.format = format;
    key.type = create_info.imageType;
    key.tiling = create_info.tiling;
    key.usage = create_info.usage;
    key.flags = create_info.flags;
    {
        std::lock_guard<std::mutex> guard(g_depth_format_support_cache_mutex);
        auto it = g_depth_format_support_cache.find(key);
        if (it != g_depth_format_support_cache.end()) {
            return it->second;
        }
    }

    bool supported = false;
    if (dispatch.get_physical_device_image_format_properties2) {
        VkPhysicalDeviceImageFormatInfo2 info{};
        info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
        info.format = format;
        info.type = create_info.imageType;
        info.tiling = create_info.tiling;
        info.usage = create_info.usage;
        info.flags = create_info.flags;
        VkImageFormatProperties2 props{};
        props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
        supported = dispatch.get_physical_device_image_format_properties2(
            physical_device,
            &info,
            &props) == VK_SUCCESS;
    } else
#ifdef VK_KHR_get_physical_device_properties2
    if (dispatch.get_physical_device_image_format_properties2_khr) {
        VkPhysicalDeviceImageFormatInfo2 info{};
        info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
        info.format = format;
        info.type = create_info.imageType;
        info.tiling = create_info.tiling;
        info.usage = create_info.usage;
        info.flags = create_info.flags;
        VkImageFormatProperties2 props{};
        props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
        supported = dispatch.get_physical_device_image_format_properties2_khr(
            physical_device,
            &info,
            &props) == VK_SUCCESS;
    } else
#endif
    if (dispatch.get_physical_device_image_format_properties) {
        VkImageFormatProperties props{};
        supported = dispatch.get_physical_device_image_format_properties(
            physical_device,
            format,
            create_info.imageType,
            create_info.tiling,
            create_info.usage,
            create_info.flags,
            &props) == VK_SUCCESS;
    }

    {
        std::lock_guard<std::mutex> guard(g_depth_format_support_cache_mutex);
        g_depth_format_support_cache[key] = supported;
    }
    return supported;
}

VkFormat choose_depth_override_format(
    VkPhysicalDevice physical_device,
    const InstanceDispatch& dispatch,
    const VkImageCreateInfo& create_info,
    int mode) {
    const VkFormat original = create_info.format;
    if (mode == DEPTH_OVERRIDE_NONE ||
        !is_depth_stencil_format(original) ||
        (create_info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
        return original;
    }
    if (mode == DEPTH_OVERRIDE_SAFE) {
        if ((original == VK_FORMAT_D24_UNORM_S8_UINT ||
             original == VK_FORMAT_D32_SFLOAT_S8_UINT) &&
            supports_image_format_for_create(
                physical_device,
                dispatch,
                VK_FORMAT_D16_UNORM_S8_UINT,
                create_info)) {
            return VK_FORMAT_D16_UNORM_S8_UINT;
        }
        if (original == VK_FORMAT_D32_SFLOAT &&
            supports_image_format_for_create(
                physical_device,
                dispatch,
                VK_FORMAT_D16_UNORM,
                create_info)) {
            return VK_FORMAT_D16_UNORM;
        }
    } else if (mode == DEPTH_OVERRIDE_AGGRESSIVE) {
        if ((original == VK_FORMAT_D32_SFLOAT ||
             original == VK_FORMAT_D24_UNORM_S8_UINT ||
             original == VK_FORMAT_D32_SFLOAT_S8_UINT ||
             original == VK_FORMAT_D16_UNORM_S8_UINT) &&
            supports_image_format_for_create(
                physical_device,
                dispatch,
                VK_FORMAT_D16_UNORM,
                create_info)) {
            return VK_FORMAT_D16_UNORM;
        }
    }
    return original;
}

bool has_incompatible_external_image_request(const void* pNext) {
    for (auto* current = reinterpret_cast<const VkBaseInStructure*>(pNext);
         current;
         current = current->pNext) {
        if (current->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO) {
            auto* external_info = reinterpret_cast<const VkPhysicalDeviceExternalImageFormatInfo*>(current);
            if (external_info->handleType != 0) {
                return true;
            }
        }
#if defined(__ANDROID__)
        if (current->sType == VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID) {
            const auto* external_format =
                reinterpret_cast<const VkExternalFormatANDROID*>(current);
            if (external_format->externalFormat != 0) {
                return true;
            }
        }
#endif
    }
    return false;
}

bool has_external_memory_image_create_request(const VkImageCreateInfo& info) {
    for (auto* current = reinterpret_cast<const VkBaseInStructure*>(info.pNext);
         current;
         current = current->pNext) {
        if (current->sType == VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO) {
            const auto* external_info =
                reinterpret_cast<const VkExternalMemoryImageCreateInfo*>(current);
            if (external_info->handleTypes != 0) {
                return true;
            }
        }
#ifdef VK_NV_external_memory
        if (current->sType == VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_NV) {
            const auto* external_info =
                reinterpret_cast<const VkExternalMemoryImageCreateInfoNV*>(current);
            if (external_info->handleTypes != 0) {
                return true;
            }
        }
#endif
    }
    return false;
}

void warn_virtual_bcn_external_query_once(const char* api_name, VkFormat format) {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true)) {
        EXYNOS_LOGW(
            "%s rejected external-memory BCn virtualization query for format %d. "
            "Virtual BCn images use an internal decoded backing image and cannot safely advertise external/AHB handles.",
            api_name,
            static_cast<int>(format));
    }
}

#if defined(__ANDROID__)
uint64_t android_hardware_buffer_usage_for_image_query(const VkPhysicalDeviceImageFormatInfo2* info) {
    if (!info) {
        return 0;
    }

    uint64_t usage = 0;
    if ((info->usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) != 0) {
        usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    }
    if ((info->usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0) {
#if __ANDROID_API__ >= 29
        usage |= AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
#else
        usage |= AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
#endif
    }
    if ((info->usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0) {
        usage |= AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;
    }
    if ((info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0) {
        usage |= AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP;
    }
    if ((info->flags & VK_IMAGE_CREATE_PROTECTED_BIT) != 0) {
        usage |= AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT;
    }

    // Match Mesa/Leegao's conservative behavior: expose at least one GPU usage
    // bit so Android callers do not treat an otherwise usable sampled image as
    // an empty/invalid AHB usage report.
    if (usage == 0) {
        usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    }
    return usage;
}
#endif

bool virtual_bcn_image_view_type_supported(
    const VkPhysicalDeviceImageFormatInfo2& info,
    VkImageViewType view_type) {
    switch (view_type) {
        case VK_IMAGE_VIEW_TYPE_2D:
        case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
            return info.type == VK_IMAGE_TYPE_2D || info.type == VK_IMAGE_TYPE_3D;
        case VK_IMAGE_VIEW_TYPE_3D:
            return info.type == VK_IMAGE_TYPE_3D;
        case VK_IMAGE_VIEW_TYPE_CUBE:
        case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
            return info.type == VK_IMAGE_TYPE_2D &&
                   ((info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0);
        case VK_IMAGE_VIEW_TYPE_1D:
        case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
        default:
            return false;
    }
}

bool has_incompatible_image_view_format_request(const VkPhysicalDeviceImageFormatInfo2& info) {
    auto* view_info = find_struct_in_pnext_chain<VkPhysicalDeviceImageViewImageFormatInfoEXT>(
        info.pNext,
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT);
    return view_info && !virtual_bcn_image_view_type_supported(info, view_info->imageViewType);
}

void release_virtual_bcn_image_format_pnext_patch(VirtualBcnImageFormatPnextPatch* patch) {
    if (!patch) {
        return;
    }
    if (patch->cloned_pnext) {
        free_cloned_pnext_chain(patch->cloned_pnext);
    }
    patch->cloned_pnext = nullptr;
    patch->patched_view_formats.clear();
}

bool append_unique_format(std::vector<VkFormat>* formats, VkFormat format) {
    if (!formats || format == VK_FORMAT_UNDEFINED) {
        return false;
    }
    if (std::find(formats->begin(), formats->end(), format) != formats->end()) {
        return false;
    }
    formats->push_back(format);
    return true;
}

bool append_virtual_bcn_query_view_format(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    const VkPhysicalDeviceImageFormatInfo2& original_info,
    const VkPhysicalDeviceImageFormatInfo2& query_info,
    VkFormat view_format,
    std::vector<VkFormat>* out_formats) {
    if (!out_formats) {
        return false;
    }
    VkFormat translated_format = view_format;
    if (is_bcn_format(view_format)) {
        translated_format = bcn_replacement_format(
            physicalDevice,
            dispatch,
            view_format,
            original_info.type,
            original_info.tiling,
            original_info.usage,
            query_info.flags);
    }
    append_unique_format(out_formats, translated_format);

    if (is_bcn_unorm_srgb_pair_format(view_format)) {
        VkFormat paired_format = is_bcn_srgb_format(view_format)
            ? bcn_unorm_variant(view_format)
            : bcn_srgb_variant(view_format);
        paired_format = bcn_replacement_format(
            physicalDevice,
            dispatch,
            paired_format,
            original_info.type,
            original_info.tiling,
            original_info.usage,
            query_info.flags);
        append_unique_format(out_formats, paired_format);
    }

    return !out_formats->empty();
}

bool patch_virtual_bcn_image_format_query_pnext(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    const VkPhysicalDeviceImageFormatInfo2& original_info,
    VkPhysicalDeviceImageFormatInfo2* query_info,
    VirtualBcnImageFormatPnextPatch* patch) {
    if (!query_info || !patch) {
        return false;
    }
    if (!original_info.pNext) {
        return true;
    }

    patch->cloned_pnext = clone_pnext_chain(original_info.pNext);
    if (!patch->cloned_pnext) {
        return false;
    }
    query_info->pNext = patch->cloned_pnext;

    auto* external_info = find_struct_in_pnext_chain<VkPhysicalDeviceExternalImageFormatInfo>(
        patch->cloned_pnext,
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);
    if (external_info) {
        // Virtual BCn images are decoded into an internal backing image. Query the
        // backing format normally, then sanitize the external output properties
        // to advertise "image supported, external handle export/import unsupported".
        external_info->handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(0);
    }

    auto* format_list = find_struct_in_pnext_chain<VkImageFormatListCreateInfo>(
        patch->cloned_pnext,
        VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO);
    if (!format_list) {
        return true;
    }
    if (!format_list->pViewFormats || format_list->viewFormatCount == 0) {
        format_list->viewFormatCount = 1;
        patch->patched_view_formats = {query_info->format};
        format_list->pViewFormats = patch->patched_view_formats.data();
        return true;
    }

    patch->patched_view_formats.clear();
    append_unique_format(&patch->patched_view_formats, query_info->format);
    for (uint32_t i = 0; i < format_list->viewFormatCount; ++i) {
        append_virtual_bcn_query_view_format(
            physicalDevice,
            dispatch,
            original_info,
            *query_info,
            format_list->pViewFormats[i],
            &patch->patched_view_formats);
    }
    if (patch->patched_view_formats.empty()) {
        return false;
    }

    format_list->viewFormatCount = static_cast<uint32_t>(patch->patched_view_formats.size());
    format_list->pViewFormats = patch->patched_view_formats.data();
    return true;
}

void sanitize_virtual_bcn_output_pnext(
    const VkPhysicalDeviceImageFormatInfo2* original_info,
    void* pNext) {
    for (auto* current = reinterpret_cast<VkBaseOutStructure*>(pNext);
         current;
         current = current->pNext) {
        switch (current->sType) {
            case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES: {
                auto* external_props = reinterpret_cast<VkExternalImageFormatProperties*>(current);
                external_props->externalMemoryProperties = VkExternalMemoryProperties{};
                break;
            }
            case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT: {
                auto* cubic_props = reinterpret_cast<VkFilterCubicImageViewImageFormatPropertiesEXT*>(current);
                cubic_props->filterCubic = VK_FALSE;
                cubic_props->filterCubicMinmax = VK_FALSE;
                break;
            }
            case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES: {
                auto* ycbcr_props = reinterpret_cast<VkSamplerYcbcrConversionImageFormatProperties*>(current);
                ycbcr_props->combinedImageSamplerDescriptorCount = 1;
                break;
            }
#if defined(__ANDROID__)
            case VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID: {
                auto* ahb_usage = reinterpret_cast<VkAndroidHardwareBufferUsageANDROID*>(current);
                ahb_usage->androidHardwareBufferUsage =
                    android_hardware_buffer_usage_for_image_query(original_info);
                break;
            }
#endif
            default:
                break;
        }
    }
}

VkResult fail_virtual_bcn_image_format_query(VkImageFormatProperties2* props) {
    if (props) {
        zero_image_format_properties(&props->imageFormatProperties);
        sanitize_virtual_bcn_output_pnext(nullptr, props->pNext);
    }
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

VkResult fail_virtual_bcn_image_format_query(VkImageFormatProperties* props) {
    zero_image_format_properties(props);
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

VkResult normalize_virtual_bcn_image_format_properties(
    const VkPhysicalDeviceImageFormatInfo2& original_info,
    VkImageFormatProperties* props) {
    if (!props) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (original_info.type == VK_IMAGE_TYPE_1D) {
        zero_image_format_properties(props);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    if ((original_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
        original_info.type != VK_IMAGE_TYPE_2D) {
        zero_image_format_properties(props);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    if ((original_info.flags & kBcnVirtualUnsupportedImageFlags) != 0) {
        zero_image_format_properties(props);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    if ((original_info.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0) {
        zero_image_format_properties(props);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    if ((props->sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0) {
        zero_image_format_properties(props);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    props->sampleCounts = VK_SAMPLE_COUNT_1_BIT;

    if (original_info.type == VK_IMAGE_TYPE_2D) {
        props->maxExtent.depth = 1;
    } else if (original_info.type == VK_IMAGE_TYPE_3D) {
        props->maxArrayLayers = 1;
    }

    if ((original_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0) {
        if (props->maxArrayLayers < 6) {
            zero_image_format_properties(props);
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
        props->maxArrayLayers -= props->maxArrayLayers % 6u;
    }

    const uint32_t safe_mip_levels = mip_levels_for_extent(props->maxExtent);
    if (safe_mip_levels == 0 || props->maxMipLevels == 0) {
        zero_image_format_properties(props);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    props->maxMipLevels = std::min(props->maxMipLevels, safe_mip_levels);

    return VK_SUCCESS;
}

bool prepare_virtual_bcn_image_format_query(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    const VkPhysicalDeviceImageFormatInfo2& original_info,
    VkPhysicalDeviceImageFormatInfo2* query_info,
    bool* virtualized,
    VirtualBcnImageFormatPnextPatch* pnext_patch) {
    if (!query_info || !virtualized || !pnext_patch) {
        return false;
    }

    *virtualized = false;
    *query_info = original_info;

    // External-memory image queries must describe the real driver image
    // contract. BCn virtualization uses an internal decoded backing image
    // and cannot preserve external/AHardwareBuffer handle compatibility,
    // so leave these queries unmodified and forward them to the driver with
    // the original format and external handleType intact. This keeps the
    // query path consistent with can_virtualize_bcn_image_create_info(),
    // which already refuses to virtualize externally-backed images.
    if (has_incompatible_external_image_request(original_info.pNext)) {
        return false;
    }

    if (has_incompatible_image_view_format_request(original_info)) {
        return false;
    }

    if (!should_virtualize_bcn_format(
            physicalDevice,
            dispatch,
            original_info.format,
            original_info.type,
            original_info.tiling,
            original_info.usage,
            original_info.flags,
            snapshot_virtualization_policy_settings(),
            is_xclipse_physical(dispatch_key(physicalDevice)),
            g_lock,
            g_bcn_native_support_cache)) {
        return false;
    }

    VkImageCreateFlags replacement_flags =
        bcn_replacement_image_create_flags(original_info.type, original_info.flags);
    VkFormat replacement = bcn_replacement_format(
        physicalDevice,
        dispatch,
        original_info.format,
        original_info.type,
        original_info.tiling,
        original_info.usage,
        replacement_flags);
    if (replacement == VK_FORMAT_UNDEFINED) {
        return false;
    }

    query_info->format = replacement;
    query_info->flags = replacement_flags;
    query_info->usage |= kBcnVirtualImageInternalUsage;
    if (!patch_virtual_bcn_image_format_query_pnext(
            physicalDevice,
            dispatch,
            original_info,
            query_info,
            pnext_patch)) {
        release_virtual_bcn_image_format_pnext_patch(pnext_patch);
        *query_info = original_info;
        return false;
    }
    *virtualized = true;
    return true;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties) {
    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !dispatch.get_physical_device_image_format_properties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDeviceImageFormatInfo2 original_info{};
    original_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    original_info.format = format;
    original_info.type = type;
    original_info.tiling = tiling;
    original_info.usage = usage;
    original_info.flags = flags;

    VkPhysicalDeviceImageFormatInfo2 query_info{};
    bool virtualized = false;
    VirtualBcnImageFormatPnextPatch pnext_patch{};
    prepare_virtual_bcn_image_format_query(
        physicalDevice,
        dispatch,
        original_info,
        &query_info,
        &virtualized,
        &pnext_patch);

    if (virtualized) {
        if ((flags & kBcnVirtualUnsupportedImageFlags) != 0 ||
            (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0) {
            return fail_virtual_bcn_image_format_query(pImageFormatProperties);
        }
    }

    VkResult result = dispatch.get_physical_device_image_format_properties(
        physicalDevice,
        query_info.format,
        query_info.type,
        query_info.tiling,
        query_info.usage,
        query_info.flags,
        pImageFormatProperties);
    release_virtual_bcn_image_format_pnext_patch(&pnext_patch);
    if (result == VK_SUCCESS && virtualized) {
        result = normalize_virtual_bcn_image_format_properties(original_info, pImageFormatProperties);
    }
    return result;
}

VkResult get_image_format_properties2_via_v1_fallback(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    if (!pImageFormatInfo || !pImageFormatProperties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!dispatch.get_physical_device_image_format_properties) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkImageFormatProperties legacy_props{};
    VkResult result = dispatch.get_physical_device_image_format_properties(
        physicalDevice,
        pImageFormatInfo->format,
        pImageFormatInfo->type,
        pImageFormatInfo->tiling,
        pImageFormatInfo->usage,
        pImageFormatInfo->flags,
        &legacy_props);
    if (result == VK_SUCCESS) {
        pImageFormatProperties->imageFormatProperties = legacy_props;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_GetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !pImageFormatInfo ||
        !pImageFormatProperties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDeviceImageFormatInfo2 query_info{};
    bool virtualized = false;
    VirtualBcnImageFormatPnextPatch pnext_patch{};
    prepare_virtual_bcn_image_format_query(
        physicalDevice,
        dispatch,
        *pImageFormatInfo,
        &query_info,
        &virtualized,
        &pnext_patch);

    const bool requested_external_image_query =
        has_incompatible_external_image_request(pImageFormatInfo->pNext);
    if (is_bcn_format(pImageFormatInfo->format) &&
        has_incompatible_image_view_format_request(*pImageFormatInfo)) {
        return fail_virtual_bcn_image_format_query(pImageFormatProperties);
    }

    VkResult result = VK_ERROR_EXTENSION_NOT_PRESENT;
    if (dispatch.get_physical_device_image_format_properties2) {
        result = dispatch.get_physical_device_image_format_properties2(
            physicalDevice,
            &query_info,
            pImageFormatProperties);
    } else {
        result = get_image_format_properties2_via_v1_fallback(
            physicalDevice,
            dispatch,
            &query_info,
            pImageFormatProperties);
    }
    release_virtual_bcn_image_format_pnext_patch(&pnext_patch);
    if (result == VK_SUCCESS && virtualized) {
        if (requested_external_image_query) {
            warn_virtual_bcn_external_query_once(
                "vkGetPhysicalDeviceImageFormatProperties2",
                pImageFormatInfo->format);
        }
        result = normalize_virtual_bcn_image_format_properties(
            *pImageFormatInfo,
            &pImageFormatProperties->imageFormatProperties);
        sanitize_virtual_bcn_output_pnext(pImageFormatInfo, pImageFormatProperties->pNext);
    }
    return result;
}

#ifdef VK_KHR_get_physical_device_properties2
VKAPI_ATTR VkResult VKAPI_CALL layer_GetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !pImageFormatInfo ||
        !pImageFormatProperties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDeviceImageFormatInfo2 query_info{};
    bool virtualized = false;
    VirtualBcnImageFormatPnextPatch pnext_patch{};
    prepare_virtual_bcn_image_format_query(
        physicalDevice,
        dispatch,
        *pImageFormatInfo,
        &query_info,
        &virtualized,
        &pnext_patch);

    const bool requested_external_image_query =
        has_incompatible_external_image_request(pImageFormatInfo->pNext);
    if (is_bcn_format(pImageFormatInfo->format) &&
        has_incompatible_image_view_format_request(*pImageFormatInfo)) {
        return fail_virtual_bcn_image_format_query(pImageFormatProperties);
    }

    VkResult result = VK_ERROR_EXTENSION_NOT_PRESENT;
    if (dispatch.get_physical_device_image_format_properties2_khr) {
        result = dispatch.get_physical_device_image_format_properties2_khr(
            physicalDevice,
            &query_info,
            pImageFormatProperties);
    } else if (dispatch.get_physical_device_image_format_properties2) {
        result = dispatch.get_physical_device_image_format_properties2(
            physicalDevice,
            &query_info,
            pImageFormatProperties);
    } else {
        result = get_image_format_properties2_via_v1_fallback(
            physicalDevice,
            dispatch,
            &query_info,
            pImageFormatProperties);
    }
    release_virtual_bcn_image_format_pnext_patch(&pnext_patch);
    if (result == VK_SUCCESS && virtualized) {
        if (requested_external_image_query) {
            warn_virtual_bcn_external_query_once(
                "vkGetPhysicalDeviceImageFormatProperties2KHR",
                pImageFormatInfo->format);
        }
        result = normalize_virtual_bcn_image_format_properties(
            *pImageFormatInfo,
            &pImageFormatProperties->imageFormatProperties);
        sanitize_virtual_bcn_output_pnext(pImageFormatInfo, pImageFormatProperties->pNext);
    }
    return result;
}
#endif

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    InstanceDispatch instance_dispatch{};
    VkInstance instance = VK_NULL_HANDLE;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_map = g_physical_to_instance.find(dispatch_key(physicalDevice));
        if (it_map == g_physical_to_instance.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        auto it_inst = g_instance_dispatch.find(it_map->second);
        if (it_inst == g_instance_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        auto it_inst_handle = g_physical_to_instance_handle.find(dispatch_key(physicalDevice));
        if (it_inst_handle != g_physical_to_instance_handle.end()) {
            instance = it_inst_handle->second;
        }
        instance_dispatch = it_inst->second;
    }

    if (!instance_dispatch.create_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto* chain_info = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));
    while (chain_info &&
           (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
            chain_info->function != VK_LAYER_LINK_INFO)) {
        chain_info = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain_info->pNext));
    }
    if (!chain_info || !chain_info->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr next_gipa = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr next_gdpa = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    auto create_info_copy = clone_device_create_info(pCreateInfo);
    const VkDeviceCreateInfo* create_info_for_driver = pCreateInfo;
    bool descriptor_buffer_feature_requested =
        device_create_requests_descriptor_buffer_feature(pCreateInfo);
    bool buffer_device_address_feature_requested =
        device_create_requests_buffer_device_address_feature(pCreateInfo);

    const bool lsfg_process_active = exynos_lsfg_process_active();

    DescriptorBufferCreateSupport descriptor_buffer_support{};
    if (!lsfg_process_active) {
        descriptor_buffer_support =
            query_descriptor_buffer_create_support(physicalDevice, instance, instance_dispatch);
    }
    PhysicalRuntime physical_runtime{};
    (void)get_physical_runtime_snapshot(physicalDevice, &physical_runtime);
    InstanceRuntime app_runtime{};
    if (instance != VK_NULL_HANDLE) {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_runtime = g_instance_runtime.find(dispatch_key(instance));
        if (it_runtime != g_instance_runtime.end()) {
            app_runtime = it_runtime->second;
        }
    }
    bool should_inject_descriptor_buffer =
        kEnableDescriptorBufferFastPath &&
        !lsfg_process_active &&
        descriptor_buffer_support.extension_supported &&
        descriptor_buffer_support.descriptor_buffer_feature_supported &&
        descriptor_buffer_support.buffer_device_address_feature_supported &&
        is_xclipse_physical(dispatch_key(physicalDevice));

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_feature_ci{};
#endif
#if defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES)
    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_feature_ci{};
#elif defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR)
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address_feature_ci{};
#endif
    std::vector<const char*> enabled_extensions;
    bool extension_list_patched = false;

    enabled_extensions.reserve(create_info_copy.enabledExtensionCount + 1u);
    for (uint32_t i = 0; i < create_info_copy.enabledExtensionCount; ++i) {
        const char* extension_name = create_info_copy.ppEnabledExtensionNames
            ? create_info_copy.ppEnabledExtensionNames[i]
            : nullptr;
        if (should_hide_device_extension(physical_runtime, &app_runtime, extension_name)) {
            extension_list_patched = true;
            EXYNOS_LOGI(
                "Driver quirk hid device extension %s for driverID=%d engine='%s'.",
                extension_name,
                static_cast<int>(physical_runtime.driver_id),
                app_runtime.engine_name.c_str());
            continue;
        }
        enabled_extensions.push_back(extension_name);
    }

    if (should_inject_descriptor_buffer) {
        if (!has_enabled_device_extension(create_info_copy.ptr(), "VK_EXT_descriptor_buffer")) {
            enabled_extensions.push_back("VK_EXT_descriptor_buffer");
            extension_list_patched = true;
        }

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
        auto* descriptor_buffer_features =
            find_struct_in_pnext_chain<VkPhysicalDeviceDescriptorBufferFeaturesEXT>(
                const_cast<void*>(create_info_copy.pNext),
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT);
        if (descriptor_buffer_features) {
            descriptor_buffer_features->descriptorBuffer = VK_TRUE;
        } else {
            descriptor_buffer_feature_ci.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
            descriptor_buffer_feature_ci.descriptorBuffer = VK_TRUE;
            prepend_struct_to_pnext_chain(&create_info_copy.pNext, &descriptor_buffer_feature_ci);
        }
        descriptor_buffer_feature_requested = true;
#endif

#if defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES)
        auto* buffer_device_address_features =
            find_struct_in_pnext_chain<VkPhysicalDeviceBufferDeviceAddressFeatures>(
                const_cast<void*>(create_info_copy.pNext),
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
        if (buffer_device_address_features) {
            buffer_device_address_features->bufferDeviceAddress = VK_TRUE;
        } else {
            buffer_device_address_feature_ci.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
            buffer_device_address_feature_ci.bufferDeviceAddress = VK_TRUE;
            prepend_struct_to_pnext_chain(&create_info_copy.pNext, &buffer_device_address_feature_ci);
        }
        buffer_device_address_feature_requested = true;
#elif defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR)
        auto* buffer_device_address_features =
            find_struct_in_pnext_chain<VkPhysicalDeviceBufferDeviceAddressFeaturesKHR>(
                const_cast<void*>(create_info_copy.pNext),
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR);
        if (buffer_device_address_features) {
            buffer_device_address_features->bufferDeviceAddress = VK_TRUE;
        } else {
            buffer_device_address_feature_ci.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
            buffer_device_address_feature_ci.bufferDeviceAddress = VK_TRUE;
            prepend_struct_to_pnext_chain(&create_info_copy.pNext, &buffer_device_address_feature_ci);
        }
        buffer_device_address_feature_requested = true;
#endif

        create_info_for_driver = create_info_copy.ptr();
    }
    if (extension_list_patched) {
        create_info_copy.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        create_info_copy.ppEnabledExtensionNames = enabled_extensions.data();
        create_info_for_driver = create_info_copy.ptr();
    }

    VkResult result = instance_dispatch.create_device(
        physicalDevice,
        create_info_for_driver,
        pAllocator,
        pDevice);
    if (result != VK_SUCCESS || !pDevice || *pDevice == VK_NULL_HANDLE) {
        return result;
    }

    DeviceDispatch device_dispatch{};
    device_dispatch.get_device_proc_addr = next_gdpa;
    device_dispatch.destroy_device = reinterpret_cast<PFN_vkDestroyDevice>(
        next_gdpa(*pDevice, "vkDestroyDevice"));
    device_dispatch.bind_buffer_memory = reinterpret_cast<PFN_vkBindBufferMemory>(
        next_gdpa(*pDevice, "vkBindBufferMemory"));
    device_dispatch.bind_buffer_memory2 = reinterpret_cast<PFN_vkBindBufferMemory2>(
        next_gdpa(*pDevice, "vkBindBufferMemory2"));
#ifdef VK_KHR_bind_memory2
    device_dispatch.bind_buffer_memory2_khr = reinterpret_cast<PFN_vkBindBufferMemory2KHR>(
        next_gdpa(*pDevice, "vkBindBufferMemory2KHR"));
#endif
    device_dispatch.map_memory = reinterpret_cast<PFN_vkMapMemory>(
        next_gdpa(*pDevice, "vkMapMemory"));
    device_dispatch.unmap_memory = reinterpret_cast<PFN_vkUnmapMemory>(
        next_gdpa(*pDevice, "vkUnmapMemory"));
    device_dispatch.create_image = reinterpret_cast<PFN_vkCreateImage>(
        next_gdpa(*pDevice, "vkCreateImage"));
    device_dispatch.destroy_image = reinterpret_cast<PFN_vkDestroyImage>(
        next_gdpa(*pDevice, "vkDestroyImage"));
    device_dispatch.create_image_view = reinterpret_cast<PFN_vkCreateImageView>(
        next_gdpa(*pDevice, "vkCreateImageView"));
    device_dispatch.destroy_image_view = reinterpret_cast<PFN_vkDestroyImageView>(
        next_gdpa(*pDevice, "vkDestroyImageView"));
    device_dispatch.create_render_pass = reinterpret_cast<PFN_vkCreateRenderPass>(
        next_gdpa(*pDevice, "vkCreateRenderPass"));
    device_dispatch.destroy_render_pass = reinterpret_cast<PFN_vkDestroyRenderPass>(
        next_gdpa(*pDevice, "vkDestroyRenderPass"));
    device_dispatch.create_framebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(
        next_gdpa(*pDevice, "vkCreateFramebuffer"));
    device_dispatch.destroy_framebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(
        next_gdpa(*pDevice, "vkDestroyFramebuffer"));
    device_dispatch.create_sampler = reinterpret_cast<PFN_vkCreateSampler>(
        next_gdpa(*pDevice, "vkCreateSampler"));
    device_dispatch.destroy_sampler = reinterpret_cast<PFN_vkDestroySampler>(
        next_gdpa(*pDevice, "vkDestroySampler"));
    device_dispatch.create_command_pool = reinterpret_cast<PFN_vkCreateCommandPool>(
        next_gdpa(*pDevice, "vkCreateCommandPool"));
    device_dispatch.destroy_command_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(
        next_gdpa(*pDevice, "vkDestroyCommandPool"));
    device_dispatch.reset_command_pool = reinterpret_cast<PFN_vkResetCommandPool>(
        next_gdpa(*pDevice, "vkResetCommandPool"));
    device_dispatch.begin_command_buffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(
        next_gdpa(*pDevice, "vkBeginCommandBuffer"));
    device_dispatch.reset_command_buffer = reinterpret_cast<PFN_vkResetCommandBuffer>(
        next_gdpa(*pDevice, "vkResetCommandBuffer"));
    device_dispatch.allocate_command_buffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(
        next_gdpa(*pDevice, "vkAllocateCommandBuffers"));
    device_dispatch.free_command_buffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(
        next_gdpa(*pDevice, "vkFreeCommandBuffers"));
    device_dispatch.create_shader_module = reinterpret_cast<PFN_vkCreateShaderModule>(
        next_gdpa(*pDevice, "vkCreateShaderModule"));
    device_dispatch.destroy_shader_module = reinterpret_cast<PFN_vkDestroyShaderModule>(
        next_gdpa(*pDevice, "vkDestroyShaderModule"));
    device_dispatch.create_descriptor_set_layout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(
        next_gdpa(*pDevice, "vkCreateDescriptorSetLayout"));
    device_dispatch.destroy_descriptor_set_layout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(
        next_gdpa(*pDevice, "vkDestroyDescriptorSetLayout"));
    device_dispatch.get_descriptor_set_layout_size_ext = reinterpret_cast<PFN_vkGetDescriptorSetLayoutSizeEXT>(
        next_gdpa(*pDevice, "vkGetDescriptorSetLayoutSizeEXT"));
    device_dispatch.get_descriptor_set_layout_binding_offset_ext =
        reinterpret_cast<PFN_vkGetDescriptorSetLayoutBindingOffsetEXT>(
            next_gdpa(*pDevice, "vkGetDescriptorSetLayoutBindingOffsetEXT"));
    device_dispatch.get_descriptor_ext = reinterpret_cast<PFN_vkGetDescriptorEXT>(
        next_gdpa(*pDevice, "vkGetDescriptorEXT"));
    device_dispatch.create_pipeline_layout = reinterpret_cast<PFN_vkCreatePipelineLayout>(
        next_gdpa(*pDevice, "vkCreatePipelineLayout"));
    device_dispatch.destroy_pipeline_layout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(
        next_gdpa(*pDevice, "vkDestroyPipelineLayout"));
    device_dispatch.create_pipeline_cache = reinterpret_cast<PFN_vkCreatePipelineCache>(
        next_gdpa(*pDevice, "vkCreatePipelineCache"));
    device_dispatch.destroy_pipeline_cache = reinterpret_cast<PFN_vkDestroyPipelineCache>(
        next_gdpa(*pDevice, "vkDestroyPipelineCache"));
    device_dispatch.get_pipeline_cache_data = reinterpret_cast<PFN_vkGetPipelineCacheData>(
        next_gdpa(*pDevice, "vkGetPipelineCacheData"));
    device_dispatch.create_graphics_pipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(
        next_gdpa(*pDevice, "vkCreateGraphicsPipelines"));
    device_dispatch.create_compute_pipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(
        next_gdpa(*pDevice, "vkCreateComputePipelines"));
    device_dispatch.destroy_pipeline = reinterpret_cast<PFN_vkDestroyPipeline>(
        next_gdpa(*pDevice, "vkDestroyPipeline"));
    device_dispatch.create_query_pool = reinterpret_cast<PFN_vkCreateQueryPool>(
        next_gdpa(*pDevice, "vkCreateQueryPool"));
    device_dispatch.destroy_query_pool = reinterpret_cast<PFN_vkDestroyQueryPool>(
        next_gdpa(*pDevice, "vkDestroyQueryPool"));
    device_dispatch.get_query_pool_results = reinterpret_cast<PFN_vkGetQueryPoolResults>(
        next_gdpa(*pDevice, "vkGetQueryPoolResults"));
    device_dispatch.create_descriptor_pool = reinterpret_cast<PFN_vkCreateDescriptorPool>(
        next_gdpa(*pDevice, "vkCreateDescriptorPool"));
    device_dispatch.destroy_descriptor_pool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(
        next_gdpa(*pDevice, "vkDestroyDescriptorPool"));
    device_dispatch.allocate_descriptor_sets = reinterpret_cast<PFN_vkAllocateDescriptorSets>(
        next_gdpa(*pDevice, "vkAllocateDescriptorSets"));
    device_dispatch.free_descriptor_sets = reinterpret_cast<PFN_vkFreeDescriptorSets>(
        next_gdpa(*pDevice, "vkFreeDescriptorSets"));
    device_dispatch.update_descriptor_sets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(
        next_gdpa(*pDevice, "vkUpdateDescriptorSets"));
    device_dispatch.get_buffer_device_address = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(
        next_gdpa(*pDevice, "vkGetBufferDeviceAddress"));
#ifdef VK_KHR_buffer_device_address
    device_dispatch.get_buffer_device_address_khr =
        reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
            next_gdpa(*pDevice, "vkGetBufferDeviceAddressKHR"));
#endif
    device_dispatch.cmd_bind_pipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(
        next_gdpa(*pDevice, "vkCmdBindPipeline"));
    device_dispatch.cmd_bind_descriptor_sets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(
        next_gdpa(*pDevice, "vkCmdBindDescriptorSets"));
    device_dispatch.cmd_bind_descriptor_buffers_ext = reinterpret_cast<PFN_vkCmdBindDescriptorBuffersEXT>(
        next_gdpa(*pDevice, "vkCmdBindDescriptorBuffersEXT"));
    device_dispatch.cmd_set_descriptor_buffer_offsets_ext =
        reinterpret_cast<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(
            next_gdpa(*pDevice, "vkCmdSetDescriptorBufferOffsetsEXT"));
    device_dispatch.cmd_push_constants = reinterpret_cast<PFN_vkCmdPushConstants>(
        next_gdpa(*pDevice, "vkCmdPushConstants"));
    device_dispatch.cmd_dispatch = reinterpret_cast<PFN_vkCmdDispatch>(
        next_gdpa(*pDevice, "vkCmdDispatch"));
    device_dispatch.cmd_reset_query_pool = reinterpret_cast<PFN_vkCmdResetQueryPool>(
        next_gdpa(*pDevice, "vkCmdResetQueryPool"));
    device_dispatch.cmd_write_timestamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
        next_gdpa(*pDevice, "vkCmdWriteTimestamp"));
    device_dispatch.cmd_pipeline_barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(
        next_gdpa(*pDevice, "vkCmdPipelineBarrier"));
    device_dispatch.cmd_pipeline_barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(
        next_gdpa(*pDevice, "vkCmdPipelineBarrier2"));
#ifdef VK_KHR_synchronization2
    device_dispatch.cmd_pipeline_barrier2_khr = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
        next_gdpa(*pDevice, "vkCmdPipelineBarrier2KHR"));
#endif
    device_dispatch.cmd_copy_buffer = reinterpret_cast<PFN_vkCmdCopyBuffer>(
        next_gdpa(*pDevice, "vkCmdCopyBuffer"));
    device_dispatch.cmd_copy_image = reinterpret_cast<PFN_vkCmdCopyImage>(
        next_gdpa(*pDevice, "vkCmdCopyImage"));
    device_dispatch.cmd_clear_depth_stencil_image = reinterpret_cast<PFN_vkCmdClearDepthStencilImage>(
        next_gdpa(*pDevice, "vkCmdClearDepthStencilImage"));
    device_dispatch.cmd_copy_image2 = reinterpret_cast<PFN_vkCmdCopyImage2>(
        next_gdpa(*pDevice, "vkCmdCopyImage2"));
    device_dispatch.cmd_blit_image = reinterpret_cast<PFN_vkCmdBlitImage>(
        next_gdpa(*pDevice, "vkCmdBlitImage"));
    device_dispatch.cmd_blit_image2 = reinterpret_cast<PFN_vkCmdBlitImage2>(
        next_gdpa(*pDevice, "vkCmdBlitImage2"));
    device_dispatch.cmd_copy_buffer_to_image = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage"));
    device_dispatch.cmd_copy_buffer_to_image2 = reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage2"));
#ifdef VK_KHR_copy_commands2
    device_dispatch.cmd_copy_image2_khr = reinterpret_cast<PFN_vkCmdCopyImage2KHR>(
        next_gdpa(*pDevice, "vkCmdCopyImage2KHR"));
    device_dispatch.cmd_blit_image2_khr = reinterpret_cast<PFN_vkCmdBlitImage2KHR>(
        next_gdpa(*pDevice, "vkCmdBlitImage2KHR"));
    device_dispatch.cmd_copy_buffer_to_image2_khr = reinterpret_cast<PFN_vkCmdCopyBufferToImage2KHR>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage2KHR"));
#endif

    DeviceRuntime runtime{};
    runtime.driver_id = physical_runtime.driver_id;
    runtime.app = app_runtime;
    if (instance != VK_NULL_HANDLE && instance_dispatch.get_instance_proc_addr) {
        runtime.descriptor_buffer_enabled = has_enabled_device_extension(
            create_info_for_driver,
            "VK_EXT_descriptor_buffer");

        if (instance_dispatch.get_physical_device_properties) {
            VkPhysicalDeviceProperties props{};
            instance_dispatch.get_physical_device_properties(physicalDevice, &props);
            runtime.vendor_id = props.vendorID;
            runtime.is_xclipse = (props.vendorID == 0x144D) || (std::strstr(props.deviceName, "Xclipse") != nullptr);
            runtime.timestamp_period = props.limits.timestampPeriod;
        }
        if (instance_dispatch.get_physical_device_features) {
            VkPhysicalDeviceFeatures features{};
            instance_dispatch.get_physical_device_features(physicalDevice, &features);
            runtime.geometry_shader = (features.geometryShader == VK_TRUE);
            runtime.tessellation_shader = (features.tessellationShader == VK_TRUE);
            runtime.shader_storage_image_write_without_format =
                (features.shaderStorageImageWriteWithoutFormat == VK_TRUE);
        }

        auto get_physical_device_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            instance_dispatch.get_instance_proc_addr(instance, "vkGetPhysicalDeviceFeatures2"));
        if (get_physical_device_features2) {
            VkPhysicalDeviceFeatures2 features2{};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT
            VkPhysicalDeviceTransformFeedbackFeaturesEXT tf_features{};
            tf_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
            prepend_struct_to_pnext_chain(&features2.pNext, &tf_features);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES
            VkPhysicalDeviceSubgroupSizeControlFeatures subgroup_size_features{};
            subgroup_size_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;
            prepend_struct_to_pnext_chain(&features2.pNext, &subgroup_size_features);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
            VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features{};
            descriptor_buffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
            prepend_struct_to_pnext_chain(&features2.pNext, &descriptor_buffer_features);
#endif

            get_physical_device_features2(physicalDevice, &features2);
            runtime.geometry_shader = (features2.features.geometryShader == VK_TRUE);
            runtime.tessellation_shader = (features2.features.tessellationShader == VK_TRUE);
            runtime.shader_storage_image_write_without_format =
                (features2.features.shaderStorageImageWriteWithoutFormat == VK_TRUE);

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT
            runtime.transform_feedback = (tf_features.transformFeedback == VK_TRUE);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES
            runtime.subgroup_size_control = (subgroup_size_features.subgroupSizeControl == VK_TRUE);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
            runtime.descriptor_buffer_supported =
                runtime.descriptor_buffer_enabled &&
                descriptor_buffer_feature_requested &&
                buffer_device_address_feature_requested &&
                (descriptor_buffer_features.descriptorBuffer == VK_TRUE);
#endif
        }

        if (instance_dispatch.get_physical_device_properties2) {
            VkPhysicalDeviceProperties2 properties2{};
            properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES
            VkPhysicalDeviceDriverProperties driver_props{};
            driver_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            prepend_struct_to_pnext_chain(&properties2.pNext, &driver_props);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES
            VkPhysicalDeviceSubgroupSizeControlProperties subgroup_size_props{};
            subgroup_size_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;
            prepend_struct_to_pnext_chain(&properties2.pNext, &subgroup_size_props);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT
            VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_props{};
            descriptor_buffer_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
            prepend_struct_to_pnext_chain(&properties2.pNext, &descriptor_buffer_props);
#endif

            instance_dispatch.get_physical_device_properties2(physicalDevice, &properties2);
            runtime.vendor_id = properties2.properties.vendorID;
            runtime.is_xclipse =
                (properties2.properties.vendorID == 0x144D) ||
                (std::strstr(properties2.properties.deviceName, "Xclipse") != nullptr);
            runtime.timestamp_period = properties2.properties.limits.timestampPeriod;
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES
            runtime.driver_id = driver_props.driverID;
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES
            runtime.min_subgroup_size = subgroup_size_props.minSubgroupSize;
            runtime.max_subgroup_size = subgroup_size_props.maxSubgroupSize;
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT
            runtime.descriptor_buffer_offset_alignment = descriptor_buffer_props.descriptorBufferOffsetAlignment;
            runtime.storage_image_descriptor_size = descriptor_buffer_props.storageImageDescriptorSize;
            runtime.storage_buffer_descriptor_size = descriptor_buffer_props.storageBufferDescriptorSize;
            runtime.combined_image_sampler_descriptor_size =
                descriptor_buffer_props.combinedImageSamplerDescriptorSize;
#endif
        }
    }

    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_device_dispatch[dispatch_key(*pDevice)] = device_dispatch;
        g_device_runtime[dispatch_key(*pDevice)] = runtime;
        g_device_to_instance_handle[dispatch_key(*pDevice)] = instance;
        g_device_to_physical_handle[dispatch_key(*pDevice)] = physicalDevice;
    }
    prewarm_compute_runtime_if_needed(*pDevice, device_dispatch, runtime.is_xclipse);
    (void)next_gipa;
    EXYNOS_LOGI(
        "Device runtime app context: app='%s' engine='%s' dxvk=%d dxvk2=%d vkd3d=%d clvk=%d.",
        runtime.app.application_name.c_str(),
        runtime.app.engine_name.c_str(),
        static_cast<int>(runtime.app.is_dxvk),
        static_cast<int>(runtime.app.is_dxvk_2_or_newer),
        static_cast<int>(runtime.app.is_vkd3d_proton),
        static_cast<int>(runtime.app.is_clvk));
    if (runtime.is_xclipse) {
        EXYNOS_LOGI(
            "Xclipse device detected (vendor=0x%04x, driverID=%d, geom=%d, tess=%d, tfb=%d, storageWriteNoFormat=%d, subgroupCtrl=%d, subgroupRange=%u..%u, descriptorBuffer=%d/%d)",
            runtime.vendor_id,
            static_cast<int>(runtime.driver_id),
            static_cast<int>(runtime.geometry_shader),
            static_cast<int>(runtime.tessellation_shader),
            static_cast<int>(runtime.transform_feedback),
            static_cast<int>(runtime.shader_storage_image_write_without_format),
            static_cast<int>(runtime.subgroup_size_control),
            runtime.min_subgroup_size,
            runtime.max_subgroup_size,
            static_cast<int>(runtime.descriptor_buffer_supported),
            static_cast<int>(runtime.descriptor_buffer_enabled));
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {
    DeviceDispatch dispatch{};
    std::shared_ptr<ComputeRuntime> compute_runtime;
    std::shared_ptr<VmaRuntime> vma_runtime;
    std::vector<VkImageView> storage_views_to_destroy;
    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        void* device_key = dispatch_key(device);
        auto it = g_device_dispatch.find(device_key);
        if (it == g_device_dispatch.end()) {
            return;
        }
        dispatch = it->second;
        g_device_dispatch.erase(it);
        g_device_runtime.erase(device_key);
        g_device_to_instance_handle.erase(device_key);
        g_device_to_physical_handle.erase(device_key);
        for (auto buffer_it = g_buffer_bindings.begin(); buffer_it != g_buffer_bindings.end();) {
            if (dispatch_key(buffer_it->second.device) == device_key) {
                buffer_it = g_buffer_bindings.erase(buffer_it);
            } else {
                ++buffer_it;
            }
        }
        for (auto memory_it = g_memory_maps.begin(); memory_it != g_memory_maps.end();) {
            if (dispatch_key(memory_it->second.device) == device_key) {
                memory_it = g_memory_maps.erase(memory_it);
            } else {
                ++memory_it;
            }
        }
        for (auto image_it = g_image_to_device.begin(); image_it != g_image_to_device.end();) {
            if (image_it->second == device_key) {
                g_virtual_images.erase(image_it->first);
                g_tracked_images.erase(image_it->first);
                g_decode_image_state.erase(image_it->first);
                image_it = g_image_to_device.erase(image_it);
            } else {
                ++image_it;
            }
        }
        for (auto view_it = g_storage_views.begin(); view_it != g_storage_views.end();) {
            auto image_owner_it = g_image_to_device.find(view_it->first.image);
            if (image_owner_it == g_image_to_device.end() || image_owner_it->second == device_key) {
                storage_views_to_destroy.push_back(view_it->second);
                view_it = g_storage_views.erase(view_it);
            } else {
                ++view_it;
            }
        }
        auto compute_it = g_compute_runtime.find(device_key);
        if (compute_it != g_compute_runtime.end()) {
            compute_runtime = compute_it->second;
            g_compute_runtime.erase(compute_it);
        }
        auto vma_it = g_vma_runtime.find(device_key);
        if (vma_it != g_vma_runtime.end()) {
            vma_runtime = vma_it->second;
            g_vma_runtime.erase(vma_it);
        }
        std::vector<void*> command_buffer_keys_to_release;
        collect_command_buffers_for_device(device_key, &command_buffer_keys_to_release);
        for (void* command_buffer_key : command_buffer_keys_to_release) {
            take_command_buffer_staging_allocations(command_buffer_key, &staging_allocations_to_release);
            take_command_buffer_descriptor_sets(command_buffer_key, &descriptor_sets_to_release);
        }
    }

    if (vma_runtime && vma_runtime->allocator != VK_NULL_HANDLE) {
        release_staging_allocations(vma_runtime.get(), &staging_allocations_to_release);
    } else {
        staging_allocations_to_release.clear();
    }
    release_descriptor_sets(device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
    collect_gpu_microbenchmarks(device, dispatch, true);
    if (dispatch.destroy_image_view) {
        for (VkImageView view : storage_views_to_destroy) {
            dispatch.destroy_image_view(device, view, nullptr);
        }
    }
    if (compute_runtime) {
        destroy_compute_runtime(device, dispatch, compute_runtime.get(), vma_runtime.get());
    }
    if (vma_runtime && vma_runtime->allocator != VK_NULL_HANDLE) {
        destroy_vma_runtime(vma_runtime.get());
    }
    if (dispatch.destroy_device) {
        dispatch.destroy_device(device, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_BindBufferMemory(
    VkDevice device,
    VkBuffer buffer,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }
    if (!dispatch.bind_buffer_memory) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.bind_buffer_memory(device, buffer, memory, memoryOffset);
    if (result == VK_SUCCESS && buffer != VK_NULL_HANDLE) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_buffer_bindings[dispatch_key(buffer)] = TrackedBufferBinding{device, memory, memoryOffset};
    }
    return result;
}

template <typename BindBufferMemoryInfo>
VkResult bind_buffer_memory2_common(
    VkDevice device,
    uint32_t bindInfoCount,
    const BindBufferMemoryInfo* pBindInfos,
    PFN_vkBindBufferMemory2 dispatch_call) {
    if (!dispatch_call) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = dispatch_call(
        device,
        bindInfoCount,
        reinterpret_cast<const VkBindBufferMemoryInfo*>(pBindInfos));
    if (result == VK_SUCCESS && pBindInfos) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        for (uint32_t i = 0; i < bindInfoCount; ++i) {
            if (pBindInfos[i].buffer != VK_NULL_HANDLE) {
                g_buffer_bindings[dispatch_key(pBindInfos[i].buffer)] =
                    TrackedBufferBinding{device, pBindInfos[i].memory, pBindInfos[i].memoryOffset};
            }
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_BindBufferMemory2(
    VkDevice device,
    uint32_t bindInfoCount,
    const VkBindBufferMemoryInfo* pBindInfos) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }
    return bind_buffer_memory2_common(device, bindInfoCount, pBindInfos, dispatch.bind_buffer_memory2);
}

#ifdef VK_KHR_bind_memory2
VKAPI_ATTR VkResult VKAPI_CALL layer_BindBufferMemory2KHR(
    VkDevice device,
    uint32_t bindInfoCount,
    const VkBindBufferMemoryInfoKHR* pBindInfos) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }
    return bind_buffer_memory2_common(
        device,
        bindInfoCount,
        pBindInfos,
        reinterpret_cast<PFN_vkBindBufferMemory2>(dispatch.bind_buffer_memory2_khr));
}
#endif

VKAPI_ATTR VkResult VKAPI_CALL layer_MapMemory(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkMemoryMapFlags flags,
    void** ppData) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }
    if (!dispatch.map_memory) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.map_memory(device, memory, offset, size, flags, ppData);
    if (result == VK_SUCCESS && memory != VK_NULL_HANDLE && ppData && *ppData) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_memory_maps[dispatch_key(memory)] = TrackedMemoryMap{device, offset, size, *ppData};
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL layer_UnmapMemory(
    VkDevice device,
    VkDeviceMemory memory) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it != g_device_dispatch.end()) {
            dispatch = it->second;
        }
    }
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_memory_maps.erase(dispatch_key(memory));
    }
    if (dispatch.unmap_memory) {
        dispatch.unmap_memory(device, memory);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateGraphicsPipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {
    DeviceDispatch dispatch{};
    DeviceRuntime device_runtime{};
    bool has_device_runtime = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
        auto it_runtime = g_device_runtime.find(dispatch_key(device));
        if (it_runtime != g_device_runtime.end()) {
            device_runtime = it_runtime->second;
            has_device_runtime = true;
        }
    }

    if (!dispatch.create_graphics_pipelines) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!pCreateInfos || createInfoCount == 0) {
        return dispatch.create_graphics_pipelines(
            device,
            pipelineCache,
            createInfoCount,
            pCreateInfos,
            pAllocator,
            pPipelines);
    }

    ClonedGraphicsPipelineCreateInfos cloned_create_infos =
        clone_graphics_pipeline_create_infos(pCreateInfos, createInfoCount);
    const GraphicsPipelineInspectionResult inspection_result =
        inspect_and_patch_graphics_pipeline_create_infos(
            &cloned_create_infos,
            has_device_runtime ? &device_runtime : nullptr);
    if (inspection_result.sanitized_empty_specialization_infos != 0) {
        EXYNOS_LOGI(
            "Sanitized %u empty graphics stage specialization blocks before vkCreateGraphicsPipelines.",
            inspection_result.sanitized_empty_specialization_infos);
    }
    if (inspection_result.removed_invalid_subgroup_size_infos != 0) {
        EXYNOS_LOGI(
            "Removed %u invalid graphics stage required subgroup-size pNext blocks before vkCreateGraphicsPipelines.",
            inspection_result.removed_invalid_subgroup_size_infos);
    }
    if (inspection_result.removed_rendering_pnext_infos != 0) {
        EXYNOS_LOGI(
            "Removed %u VkPipelineRenderingCreateInfo blocks from graphics pipelines that already used renderPass.",
            inspection_result.removed_rendering_pnext_infos);
    }
    if (inspection_result.sanitized_zero_color_attachment_blend_states != 0) {
        EXYNOS_LOGI(
            "Cleared %u graphics color blend attachment arrays for zero-color dynamic rendering pipelines.",
            inspection_result.sanitized_zero_color_attachment_blend_states);
    }
    if (inspection_result.clamped_rendering_color_blend_attachment_counts != 0) {
        EXYNOS_LOGI(
            "Clamped %u graphics color blend attachment counts to match dynamic rendering colorAttachmentCount.",
            inspection_result.clamped_rendering_color_blend_attachment_counts);
    }
    if (inspection_result.sanitized_empty_dynamic_state_arrays != 0) {
        EXYNOS_LOGI(
            "Cleared %u empty graphics dynamic state arrays before vkCreateGraphicsPipelines.",
            inspection_result.sanitized_empty_dynamic_state_arrays);
    }
    if (inspection_result.sanitized_dynamic_viewport_arrays != 0) {
        EXYNOS_LOGI(
            "Cleared %u static viewport arrays from graphics pipelines that use dynamic viewport state.",
            inspection_result.sanitized_dynamic_viewport_arrays);
    }
    if (inspection_result.sanitized_dynamic_scissor_arrays != 0) {
        EXYNOS_LOGI(
            "Cleared %u static scissor arrays from graphics pipelines that use dynamic scissor state.",
            inspection_result.sanitized_dynamic_scissor_arrays);
    }
    if (inspection_result.sanitized_empty_color_blend_arrays != 0) {
        EXYNOS_LOGI(
            "Cleared %u empty graphics color blend attachment arrays before vkCreateGraphicsPipelines.",
            inspection_result.sanitized_empty_color_blend_arrays);
    }
    if (inspection_result.sanitized_empty_specialization_map_arrays != 0) {
        EXYNOS_LOGI(
            "Cleared %u empty graphics specialization map-entry arrays before vkCreateGraphicsPipelines.",
            inspection_result.sanitized_empty_specialization_map_arrays);
    }
    if (inspection_result.sanitized_empty_specialization_data_blocks != 0) {
        EXYNOS_LOGI(
            "Cleared %u empty graphics specialization data pointers before vkCreateGraphicsPipelines.",
            inspection_result.sanitized_empty_specialization_data_blocks);
    }
    const VkGraphicsPipelineCreateInfo* forwarded_create_infos = cloned_create_infos.data();
    return dispatch.create_graphics_pipelines(
        device,
        pipelineCache,
        createInfoCount,
        forwarded_create_infos ? forwarded_create_infos : pCreateInfos,
        pAllocator,
        pPipelines);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateImage(
    VkDevice device,
    const VkImageCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImage* pImage) {
    DeviceDispatch dispatch{};
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        void* key = dispatch_key(device);
        auto it = g_device_dispatch.find(key);
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
        auto it_phys = g_device_to_physical_handle.find(key);
        if (it_phys != g_device_to_physical_handle.end()) {
            physical_device = it_phys->second;
        }
    }

    if (!dispatch.create_image) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    InstanceDispatch instance_dispatch{};
    const bool has_instance_dispatch =
        physical_device != VK_NULL_HANDLE &&
        get_instance_dispatch_for_physical(physical_device, &instance_dispatch, nullptr);

    if (physical_device != VK_NULL_HANDLE && pCreateInfo && is_bcn_format(pCreateInfo->format)) {
        const LayerLsfgCompatSnapshot lsfg_compat = snapshot_lsfg_compat();
        const bool external_memory_image =
            has_external_memory_image_create_request(*pCreateInfo);

        // LSFG owns the external-memory/AHardwareBuffer image contract.
        // A virtual BCn image is backed by an internal decoded image with a
        // replacement format, so rewriting an externally-backed BCn image
        // would break the creation/query/bind contract expected by LSFG.
        const bool bypass_bcn_virtualization =
            lsfg_compat.enabled && external_memory_image;

        if (has_instance_dispatch && !bypass_bcn_virtualization) {
            VirtualizedImageCreateResult virtualization{};
            if (try_create_virtualized_image(
                    device,
                    physical_device,
                    dispatch,
                    instance_dispatch,
                    snapshot_virtualization_policy_settings(),
                    is_xclipse_physical(dispatch_key(physical_device)),
                    pCreateInfo,
                    pAllocator,
                    pImage,
                    g_lock,
                    g_bcn_native_support_cache,
                    g_virtual_images,
                    g_tracked_images,
                    g_image_to_device,
                    g_decode_image_state,
                    &virtualization)) {
                if (virtualization.virtualized) {
                    g_virtualized_create_images.fetch_add(1);
                    record_virtualized_bcn_format(pCreateInfo->format);
                    EXYNOS_LOGI(
                        "Virtualized BCn image create (requested=%d, replacement=%d, type=%d, extent=%ux%ux%u, mips=%u, layers=%u, usage=0x%x, flags=0x%x)",
                        static_cast<int>(pCreateInfo->format),
                        static_cast<int>(virtualization.replacement),
                        static_cast<int>(pCreateInfo->imageType),
                        pCreateInfo->extent.width,
                        pCreateInfo->extent.height,
                        pCreateInfo->extent.depth,
                        pCreateInfo->mipLevels,
                        pCreateInfo->arrayLayers,
                        virtualization.actual_usage,
                        virtualization.actual_flags);
                }
                return virtualization.result;
            }
        } else {
            g_native_bcn_create_images.fetch_add(1);
        }
    }

    vku::safe_VkImageCreateInfo patched_depth_info;
    const VkImageCreateInfo* create_info_for_driver = pCreateInfo;
    bool depth_reduced = false;
    VkFormat original_depth_format = VK_FORMAT_UNDEFINED;
    VkFormat reduced_depth_format = VK_FORMAT_UNDEFINED;
    LayerSettingsSnapshot settings = snapshot_layer_settings();
    if (pCreateInfo &&
        has_instance_dispatch &&
        (pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0 &&
        is_depth_stencil_format(pCreateInfo->format) &&
        settings.depth_override_mode != DEPTH_OVERRIDE_NONE &&
        settings.depth_override_mode != DEPTH_OVERRIDE_DISABLED &&
        (!settings.xclipse_only || is_xclipse_physical(dispatch_key(physical_device)))) {
        VkFormat replacement = choose_depth_override_format(
            physical_device,
            instance_dispatch,
            *pCreateInfo,
            settings.depth_override_mode);
        if (replacement != pCreateInfo->format) {
            patched_depth_info = clone_image_create_info(pCreateInfo);
            patched_depth_info.ptr()->format = replacement;
            create_info_for_driver = patched_depth_info.ptr();
            depth_reduced = true;
            original_depth_format = pCreateInfo->format;
            reduced_depth_format = replacement;
        }
    }

    VkResult result = dispatch.create_image(device, create_info_for_driver, pAllocator, pImage);
    if (result == VK_SUCCESS && pImage && *pImage != VK_NULL_HANDLE) {
        track_created_image(
            device,
            *pImage,
            create_info_for_driver,
            g_lock,
            g_tracked_images,
            g_image_to_device,
            g_decode_image_state);
        if (depth_reduced) {
            std::lock_guard<std::shared_mutex> guard(g_lock);
            auto it = g_tracked_images.find(dispatch_key(*pImage));
            if (it != g_tracked_images.end()) {
                it->second.is_depth_stencil_reduced = true;
                it->second.original_depth_format = original_depth_format;
            }
            EXYNOS_LOGI(
                "Depth override reduced image format from %d to %d (extent=%ux%ux%u, usage=0x%x).",
                static_cast<int>(original_depth_format),
                static_cast<int>(reduced_depth_format),
                pCreateInfo->extent.width,
                pCreateInfo->extent.height,
                pCreateInfo->extent.depth,
                pCreateInfo->usage);
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyImage(
    VkDevice device,
    VkImage image,
    const VkAllocationCallbacks* pAllocator) {
    DeviceDispatch dispatch{};
    std::vector<VkImageView> storage_views_to_destroy;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return;
        }
        dispatch = it->second;
    }
    cleanup_destroyed_image_tracking(
        image,
        g_lock,
        g_virtual_images,
        g_tracked_images,
        g_image_to_device,
        g_decode_image_state,
        g_storage_views,
        &storage_views_to_destroy);

    if (dispatch.destroy_image_view) {
        for (VkImageView view : storage_views_to_destroy) {
            dispatch.destroy_image_view(device, view, nullptr);
        }
    }
    if (dispatch.destroy_image) {
        dispatch.destroy_image(device, image, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateImageView(
    VkDevice device,
    const VkImageViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImageView* pView) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }

    if (!dispatch.create_image_view) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult virtualized_view_result = VK_SUCCESS;
    if (create_virtualized_image_view(
            device,
            dispatch,
            pCreateInfo,
            pAllocator,
            pView,
            g_lock,
            g_virtual_images,
            &virtualized_view_result)) {
        return virtualized_view_result;
    }

    vku::safe_VkImageViewCreateInfo patched_depth_view;
    const VkImageViewCreateInfo* create_info_for_driver = pCreateInfo;
    if (pCreateInfo && pCreateInfo->image != VK_NULL_HANDLE) {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_image = g_tracked_images.find(dispatch_key(pCreateInfo->image));
        if (it_image != g_tracked_images.end() && it_image->second.is_depth_stencil_reduced) {
            bool needs_patch = false;
            if (pCreateInfo->format == it_image->second.original_depth_format) {
                needs_patch = true;
            }
            if (!format_has_stencil_aspect(it_image->second.format) &&
                (pCreateInfo->subresourceRange.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0) {
                needs_patch = true;
            }

            if (needs_patch) {
                patched_depth_view = clone_image_view_create_info(pCreateInfo);
                if (pCreateInfo->format == it_image->second.original_depth_format) {
                    patched_depth_view.ptr()->format = it_image->second.format;
                    EXYNOS_LOGI(
                        "Depth override remapped image view format from %d to %d.",
                        static_cast<int>(pCreateInfo->format),
                        static_cast<int>(it_image->second.format));
                }
                if (!format_has_stencil_aspect(it_image->second.format)) {
                    patched_depth_view.ptr()->subresourceRange.aspectMask &=
                        ~VK_IMAGE_ASPECT_STENCIL_BIT;
                    if (patched_depth_view.ptr()->subresourceRange.aspectMask == 0) {
                        EXYNOS_LOGW(
                            "Depth override rejected a stencil-only image view for image format reduced to %d.",
                            static_cast<int>(it_image->second.format));
                        return VK_ERROR_FORMAT_NOT_SUPPORTED;
                    }
                }
                create_info_for_driver = patched_depth_view.ptr();
            }
        }
    }

    return dispatch.create_image_view(device, create_info_for_driver, pAllocator, pView);
}

VKAPI_ATTR void VKAPI_CALL layer_CmdClearDepthStencilImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout imageLayout,
    const VkClearDepthStencilValue* pDepthStencil,
    uint32_t rangeCount,
    const VkImageSubresourceRange* pRanges) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdClearDepthStencilImage",
            &dispatch,
            &device)) {
        return;
    }
    (void)device;

    if (!dispatch.cmd_clear_depth_stencil_image) {
        return;
    }

    const VkImageSubresourceRange* ranges_for_driver = pRanges;
    std::vector<VkImageSubresourceRange> patched_ranges;
    if (image != VK_NULL_HANDLE && pRanges && rangeCount > 0) {
        VkFormat real_format = VK_FORMAT_UNDEFINED;
        bool depth_stencil_reduced = false;
        {
            std::shared_lock<std::shared_mutex> guard(g_lock);
            auto it_image = g_tracked_images.find(dispatch_key(image));
            if (it_image != g_tracked_images.end()) {
                depth_stencil_reduced = it_image->second.is_depth_stencil_reduced;
                real_format = it_image->second.format;
            }
        }

        if (depth_stencil_reduced && !format_has_stencil_aspect(real_format)) {
            patched_ranges.assign(pRanges, pRanges + rangeCount);
            bool changed = false;
            for (VkImageSubresourceRange& range : patched_ranges) {
                if ((range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0) {
                    range.aspectMask &= ~VK_IMAGE_ASPECT_STENCIL_BIT;
                    changed = true;
                }
            }
            if (changed) {
                ranges_for_driver = patched_ranges.data();
                EXYNOS_LOGI(
                    "Depth override removed stencil aspect from vkCmdClearDepthStencilImage for reduced image %p.",
                    reinterpret_cast<void*>(image));
            }
        }
    }

    dispatch.cmd_clear_depth_stencil_image(
        commandBuffer,
        image,
        imageLayout,
        pDepthStencil,
        rangeCount,
        ranges_for_driver);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateRenderPass(
    VkDevice device,
    const VkRenderPassCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkRenderPass* pRenderPass) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }

    if (!dispatch.create_render_pass) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto create_info_copy = clone_render_pass_create_info(pCreateInfo);
    return dispatch.create_render_pass(device, create_info_copy.ptr(), pAllocator, pRenderPass);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateFramebuffer(
    VkDevice device,
    const VkFramebufferCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFramebuffer* pFramebuffer) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }

    if (!dispatch.create_framebuffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto create_info_copy = clone_framebuffer_create_info(pCreateInfo);
    return dispatch.create_framebuffer(device, create_info_copy.ptr(), pAllocator, pFramebuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateSampler(
    VkDevice device,
    const VkSamplerCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSampler* pSampler) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }

    if (!dispatch.create_sampler) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto create_info_copy = clone_sampler_create_info(pCreateInfo);
    return dispatch.create_sampler(device, create_info_copy.ptr(), pAllocator, pSampler);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateCommandPool(
    VkDevice device,
    const VkCommandPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkCommandPool* pCommandPool) {
    return handle_create_command_pool(
        make_command_buffer_hook_context(), device, pCreateInfo, pAllocator, pCommandPool);
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    const VkAllocationCallbacks* pAllocator) {
    handle_destroy_command_pool(
        make_command_buffer_hook_context(), device, commandPool, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_ResetCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    VkCommandPoolResetFlags flags) {
    return handle_reset_command_pool(
        make_command_buffer_hook_context(), device, commandPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_BeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo) {
    return handle_begin_command_buffer(
        make_command_buffer_dispatch_context(),
        [](VkDevice device, VkCommandBuffer command_buffer, const DeviceDispatch& dispatch) {
            release_command_buffer_resources(device, command_buffer, dispatch);
        },
        commandBuffer,
        pBeginInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_ResetCommandBuffer(
    VkCommandBuffer commandBuffer,
    VkCommandBufferResetFlags flags) {
    return handle_reset_command_buffer(
        make_command_buffer_dispatch_context(),
        [](VkDevice device, VkCommandBuffer command_buffer, const DeviceDispatch& dispatch) {
            release_command_buffer_resources(device, command_buffer, dispatch);
        },
        commandBuffer,
        flags);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_AllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers) {
    return handle_allocate_command_buffers(
        make_command_buffer_hook_context(), device, pAllocateInfo, pCommandBuffers);
}

VKAPI_ATTR void VKAPI_CALL layer_FreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer* pCommandBuffers) {
    handle_free_command_buffers(
        make_command_buffer_hook_context(),
        device,
        commandPool,
        commandBufferCount,
        pCommandBuffers);
}

bool is_virtual_image(VkImage image) {
    std::shared_lock<std::shared_mutex> guard(g_lock);
    return g_virtual_images.find(dispatch_key(image)) != g_virtual_images.end();
}

bool try_special_copy_image_regions(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkImage src_image,
    VkImageLayout src_layout,
    VkImage dst_image,
    VkImageLayout dst_layout,
    const CopyImageRouteInfo& route,
    uint32_t region_count,
    const VkImageCopy* regions) {
    if (!regions || region_count == 0 || device == VK_NULL_HANDLE) {
        return false;
    }
    if (!route.needs_special_path || !route.can_use_special_path) {
        return false;
    }

    const bool microbenchmark_enabled = snapshot_layer_settings().microbenchmark_enabled;
    const auto benchmark_start = std::chrono::steady_clock::now();
    const DecoderShaderKind special_shader_kind = copy_image_shader_kind_for_format(route.dst_actual_format);
    float timestamp_period = 0.0f;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_runtime = g_device_runtime.find(dispatch_key(device));
        if (it_runtime != g_device_runtime.end()) {
            timestamp_period = it_runtime->second.timestamp_period;
        }
    }
    auto finish_benchmark = [&](uint32_t work_items, bool success) {
        if (!microbenchmark_enabled) {
            return success;
        }
        const auto duration_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - benchmark_start)
                .count());
        record_microbenchmark_sample(
            BenchmarkDomain::SpecialCopyCpu,
            special_shader_kind,
            duration_ns,
            work_items,
            success);
        return success;
    };

    if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
        src_layout == VK_IMAGE_LAYOUT_PREINITIALIZED ||
        dst_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
        dst_layout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        return finish_benchmark(0, false);
    }

    auto runtime = get_or_create_compute_runtime(dispatch_key(device));
    {
        std::lock_guard<std::mutex> init_guard(runtime->init_mutex);
        if (!initialize_compute_runtime(
                device,
                dispatch,
                compute_runtime_config_for_device(device),
                runtime.get())) {
            return finish_benchmark(0, false);
        }
    }
    VkPipeline special_pipeline = choose_copy_image_pipeline(*runtime, route.dst_actual_format);
    if (!runtime->available ||
        special_pipeline == VK_NULL_HANDLE ||
        runtime->copy_sampler == VK_NULL_HANDLE) {
        return finish_benchmark(0, false);
    }

    size_t planned_region_count = 0;
    for (uint32_t r = 0; r < region_count; ++r) {
        planned_region_count +=
            regions[r].srcSubresource.layerCount ? regions[r].srcSubresource.layerCount : 1u;
    }

    std::vector<PreparedSpecialCopyRegion> prepared_regions;
    prepared_regions.reserve(planned_region_count);
    for (uint32_t r = 0; r < region_count; ++r) {
        const VkImageCopy& region = regions[r];
        uint32_t src_layers = region.srcSubresource.layerCount ? region.srcSubresource.layerCount : 1u;
        uint32_t dst_layers = region.dstSubresource.layerCount ? region.dstSubresource.layerCount : 1u;
        if (src_layers != dst_layers) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return finish_benchmark(0, false);
        }
        for (uint32_t layer = 0; layer < src_layers; ++layer) {
            PreparedSpecialCopyRegion prepared{};
            if (!build_special_copy_region_plan(region, special_pipeline, special_shader_kind, layer, &prepared)) {
                release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
                return finish_benchmark(0, false);
            }
            prepared_regions.push_back(std::move(prepared));
        }
    }

    std::shared_ptr<VmaRuntime> descriptor_buffer_vma_runtime;
    if (runtime->use_descriptor_buffer) {
        descriptor_buffer_vma_runtime = get_or_create_vma_runtime(dispatch_key(device));
        std::lock_guard<std::mutex> init_guard(descriptor_buffer_vma_runtime->init_mutex);
        VmaRuntimeInitInputs vma_inputs{};
        if (!gather_vma_runtime_init_inputs(dispatch_key(device), &vma_inputs) ||
            !initialize_vma_runtime(
                vma_inputs.instance,
                vma_inputs.physical_device,
                device,
                vma_inputs.instance_dispatch,
                dispatch,
                descriptor_buffer_vma_runtime.get())) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return finish_benchmark(0, false);
        }
    }

    for (PreparedSpecialCopyRegion& prepared : prepared_regions) {
        if (!get_or_create_storage_view(
                device,
                dispatch,
                src_image,
                prepared.src_subresource_range.baseMipLevel,
                prepared.src_subresource_range.baseArrayLayer,
                route.src_actual_format,
                &prepared.src_view)) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return finish_benchmark(0, false);
        }
        if (!get_or_create_storage_view(
                device,
                dispatch,
                dst_image,
                prepared.dst_subresource_range.baseMipLevel,
                prepared.dst_subresource_range.baseArrayLayer,
                route.dst_actual_format,
                &prepared.dst_view)) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return finish_benchmark(0, false);
        }
        if (!prepare_special_copy_descriptor_set(
                device,
                dispatch,
                runtime.get(),
                descriptor_buffer_vma_runtime.get(),
                prepared.dst_view,
                prepared.src_view,
                runtime->copy_sampler,
                &prepared.descriptor_pool,
                &prepared.descriptor_set)) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return finish_benchmark(0, false);
        }
    }

    const uint32_t benchmark_work_items = static_cast<uint32_t>(prepared_regions.size());
    VkQueryPool gpu_benchmark_query_pool = VK_NULL_HANDLE;
    if (microbenchmark_enabled) {
        begin_gpu_microbenchmark(
            device,
            dispatch,
            timestamp_period,
            command_buffer,
            BenchmarkDomain::SpecialCopyGpu,
            special_shader_kind,
            benchmark_work_items,
            &gpu_benchmark_query_pool);
    }
    for (PreparedSpecialCopyRegion& prepared : prepared_regions) {
        record_special_copy_region(
            command_buffer,
            device,
            dispatch,
            runtime.get(),
            src_image,
            src_layout,
            dst_image,
            dst_layout,
            &prepared);
    }
    end_gpu_microbenchmark(device, dispatch, command_buffer, gpu_benchmark_query_pool);
    release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
    return finish_benchmark(benchmark_work_items, true);
}

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageCopy* pRegions) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdCopyImage",
            &dispatch,
            &device)) {
        return;
    }
    (void)device;

    if (!dispatch.cmd_copy_image) {
        return;
    }
    CopyImageRouteInfo route = classify_copy_image_route(
        srcImage, dstImage, g_lock, g_virtual_images, g_tracked_images);
    if (try_special_copy_image_regions(
            commandBuffer,
            device,
            dispatch,
            srcImage,
            srcImageLayout,
            dstImage,
            dstImageLayout,
            route,
            regionCount,
            pRegions)) {
        g_copy_image_calls.fetch_add(1);
        if (route.involves_virtual) {
            g_copy_image_virtual_hits.fetch_add(1);
        }
        g_copy_image_special_routes.fetch_add(1);
        maybe_log_decode_stats();
        return;
    }
    note_copy_image_route(
        "vkCmdCopyImage",
        route,
        g_copy_image_calls,
        g_copy_image_virtual_hits,
        g_copy_image_real_routes,
        g_copy_image_special_fallbacks,
        &maybe_log_decode_stats,
        &log_copy_image_route_warning);
    if (should_block_native_virtual_image_transfer(route)) {
        g_blocked_incompatible_virtual_transfers.fetch_add(1);
        return;
    }
    dispatch.cmd_copy_image(
        commandBuffer,
        srcImage,
        srcImageLayout,
        dstImage,
        dstImageLayout,
        regionCount,
        pRegions);
}

template <typename CopyImageInfo, typename DispatchCall>
void handle_copy_image2_common(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    const char* api_name,
    const CopyImageInfo* copy_info,
    DispatchCall&& dispatch_call) {
    if (!copy_info) {
        return;
    }

    TempArena<> arena;
    auto cloned_copy_info = clone_copy_image_info2(*copy_info, arena);
    const VkCopyImageInfo2* cloned_info = cloned_copy_info.ptr();

    CopyImageRouteInfo route = classify_copy_image_route(
        cloned_info->srcImage,
        cloned_info->dstImage,
        g_lock,
        g_virtual_images,
        g_tracked_images);

    arena.reset();
    VkImageCopy* regions = clone_copy_image_regions_to_legacy(*cloned_info, arena);
    if (try_special_copy_image_regions(
            command_buffer,
            device,
            dispatch,
            cloned_info->srcImage,
            cloned_info->srcImageLayout,
            cloned_info->dstImage,
            cloned_info->dstImageLayout,
            route,
            cloned_info->regionCount,
            regions)) {
        g_copy_image_calls.fetch_add(1);
        if (route.involves_virtual) {
            g_copy_image_virtual_hits.fetch_add(1);
        }
        g_copy_image_special_routes.fetch_add(1);
        maybe_log_decode_stats();
        return;
    }
    note_copy_image_route(
        api_name,
        route,
        g_copy_image_calls,
        g_copy_image_virtual_hits,
        g_copy_image_real_routes,
        g_copy_image_special_fallbacks,
        &maybe_log_decode_stats,
        &log_copy_image_route_warning);
    if (should_block_native_virtual_image_transfer(route)) {
        g_blocked_incompatible_virtual_transfers.fetch_add(1);
        return;
    }
    dispatch_call();
}

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyImageInfo2* pCopyImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdCopyImage2",
            &dispatch,
            &device)) {
        return;
    }
    (void)device;
    if (!dispatch.cmd_copy_image2 || !pCopyImageInfo) {
        return;
    }
    handle_copy_image2_common(
        commandBuffer,
        device,
        dispatch,
        "vkCmdCopyImage2",
        pCopyImageInfo,
        [&]() { dispatch.cmd_copy_image2(commandBuffer, pCopyImageInfo); });
}

#ifdef VK_KHR_copy_commands2
VKAPI_ATTR void VKAPI_CALL layer_CmdCopyImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkCopyImageInfo2KHR* pCopyImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdCopyImage2KHR",
            &dispatch,
            &device)) {
        return;
    }
    (void)device;
    if (!dispatch.cmd_copy_image2_khr || !pCopyImageInfo) {
        return;
    }
    handle_copy_image2_common(
        commandBuffer,
        device,
        dispatch,
        "vkCmdCopyImage2KHR",
        pCopyImageInfo,
        [&]() { dispatch.cmd_copy_image2_khr(commandBuffer, pCopyImageInfo); });
}
#endif

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyBufferToImage(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkBufferImageCopy* pRegions) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdCopyBufferToImage",
            &dispatch,
            &device)) {
        return;
    }

    if (dispatch.cmd_copy_buffer_to_image) {
        if (!try_decode_copy_regions(
                commandBuffer,
                device,
                dispatch,
                srcBuffer,
                dstImage,
                dstImageLayout,
                regionCount,
                pRegions) &&
            !is_virtual_image(dstImage)) {
            dispatch.cmd_copy_buffer_to_image(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
        }
    }
}

template <typename CopyBufferToImageInfo, typename DispatchCall>
void handle_copy_buffer_to_image2_common(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    const CopyBufferToImageInfo* copy_info,
    DispatchCall&& dispatch_call) {
    if (!copy_info) {
        return;
    }

    TempArena<> arena;
    auto cloned_copy_info = clone_copy_buffer_to_image_info2(*copy_info, arena);
    const VkCopyBufferToImageInfo2* cloned_info = cloned_copy_info.ptr();

    arena.reset();
    VkBufferImageCopy* regions =
        clone_copy_buffer_to_image_regions_to_legacy(*cloned_info, arena);
    if (!try_decode_copy_regions(
            command_buffer,
            device,
            dispatch,
            cloned_info->srcBuffer,
            cloned_info->dstImage,
            cloned_info->dstImageLayout,
            cloned_info->regionCount,
            regions) &&
        !is_virtual_image(cloned_info->dstImage)) {
        dispatch_call();
    }
}

VKAPI_ATTR void VKAPI_CALL layer_CmdBlitImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageBlit* pRegions,
    VkFilter filter) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdBlitImage",
            &dispatch,
            &device)) {
        return;
    }
    (void)device;
    if (!dispatch.cmd_blit_image) {
        return;
    }

    CopyImageRouteInfo route = classify_copy_image_route(
        srcImage,
        dstImage,
        g_lock,
        g_virtual_images,
        g_tracked_images);
    note_blit_image_route("vkCmdBlitImage", route);
    if (should_block_native_virtual_image_transfer(route)) {
        g_blocked_incompatible_virtual_transfers.fetch_add(1);
        return;
    }

    dispatch.cmd_blit_image(
        commandBuffer,
        srcImage,
        srcImageLayout,
        dstImage,
        dstImageLayout,
        regionCount,
        pRegions,
        filter);
}

template <typename BlitImageInfo, typename DispatchCall2, typename DispatchCallLegacy>
void handle_blit_image2_common(
    VkCommandBuffer command_buffer,
    const char* api_name,
    const BlitImageInfo* blit_info,
    bool has_dispatch2,
    DispatchCall2&& dispatch_call2,
    DispatchCallLegacy&& dispatch_call_legacy) {
    if (!blit_info) {
        return;
    }

    TempArena<> arena;
    auto cloned_blit_info = clone_blit_image_info2(*blit_info, arena);
    const VkBlitImageInfo2* cloned_info = cloned_blit_info.ptr();

    CopyImageRouteInfo route = classify_copy_image_route(
        cloned_info->srcImage,
        cloned_info->dstImage,
        g_lock,
        g_virtual_images,
        g_tracked_images);
    note_blit_image_route(api_name, route);
    if (should_block_native_virtual_image_transfer(route)) {
        g_blocked_incompatible_virtual_transfers.fetch_add(1);
        return;
    }

    if (has_dispatch2) {
        dispatch_call2(cloned_info);
        return;
    }

    arena.reset();
    VkImageBlit* legacy_regions = clone_blit_image_regions_to_legacy(*cloned_info, arena);
    dispatch_call_legacy(cloned_info, legacy_regions);
}

VKAPI_ATTR void VKAPI_CALL layer_CmdBlitImage2(
    VkCommandBuffer commandBuffer,
    const VkBlitImageInfo2* pBlitImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdBlitImage2",
            &dispatch,
            &device)) {
        return;
    }
    (void)device;
    if ((!dispatch.cmd_blit_image2 && !dispatch.cmd_blit_image) || !pBlitImageInfo) {
        return;
    }

    handle_blit_image2_common(
        commandBuffer,
        "vkCmdBlitImage2",
        pBlitImageInfo,
        dispatch.cmd_blit_image2 != nullptr,
        [&](const VkBlitImageInfo2* cloned_info) {
            if (dispatch.cmd_blit_image2) {
                dispatch.cmd_blit_image2(commandBuffer, cloned_info);
            }
        },
        [&](const VkBlitImageInfo2* cloned_info, const VkImageBlit* legacy_regions) {
            if (dispatch.cmd_blit_image) {
                dispatch.cmd_blit_image(
                    commandBuffer,
                    cloned_info->srcImage,
                    cloned_info->srcImageLayout,
                    cloned_info->dstImage,
                    cloned_info->dstImageLayout,
                    cloned_info->regionCount,
                    legacy_regions,
                    cloned_info->filter);
            }
        });
}

#ifdef VK_KHR_copy_commands2
VKAPI_ATTR void VKAPI_CALL layer_CmdBlitImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkBlitImageInfo2KHR* pBlitImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdBlitImage2KHR",
            &dispatch,
            &device)) {
        return;
    }
    (void)device;
    if ((!dispatch.cmd_blit_image2_khr && !dispatch.cmd_blit_image2 && !dispatch.cmd_blit_image) ||
        !pBlitImageInfo) {
        return;
    }

    handle_blit_image2_common(
        commandBuffer,
        "vkCmdBlitImage2KHR",
        pBlitImageInfo,
        (dispatch.cmd_blit_image2_khr != nullptr) || (dispatch.cmd_blit_image2 != nullptr),
        [&](const VkBlitImageInfo2* cloned_info) {
            if (dispatch.cmd_blit_image2_khr) {
                dispatch.cmd_blit_image2_khr(
                    commandBuffer,
                    reinterpret_cast<const VkBlitImageInfo2KHR*>(cloned_info));
            } else if (dispatch.cmd_blit_image2) {
                dispatch.cmd_blit_image2(commandBuffer, cloned_info);
            }
        },
        [&](const VkBlitImageInfo2* cloned_info, const VkImageBlit* legacy_regions) {
            if (dispatch.cmd_blit_image) {
                dispatch.cmd_blit_image(
                    commandBuffer,
                    cloned_info->srcImage,
                    cloned_info->srcImageLayout,
                    cloned_info->dstImage,
                    cloned_info->dstImageLayout,
                    cloned_info->regionCount,
                    legacy_regions,
                    cloned_info->filter);
            }
        });
}
#endif

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyBufferToImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdCopyBufferToImage2",
            &dispatch,
            &device)) {
        return;
    }

    if (dispatch.cmd_copy_buffer_to_image2 && pCopyBufferToImageInfo) {
        handle_copy_buffer_to_image2_common(
            commandBuffer,
            device,
            dispatch,
            pCopyBufferToImageInfo,
            [&]() { dispatch.cmd_copy_buffer_to_image2(commandBuffer, pCopyBufferToImageInfo); });
    }
}

#ifdef VK_KHR_copy_commands2
VKAPI_ATTR void VKAPI_CALL layer_CmdCopyBufferToImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferToImageInfo2KHR* pCopyBufferToImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_command_buffer_dispatch(
            make_command_buffer_dispatch_context(),
            commandBuffer,
            "vkCmdCopyBufferToImage2KHR",
            &dispatch,
            &device)) {
        return;
    }

    if (dispatch.cmd_copy_buffer_to_image2_khr && pCopyBufferToImageInfo) {
        handle_copy_buffer_to_image2_common(
            commandBuffer,
            device,
            dispatch,
            pCopyBufferToImageInfo,
            [&]() { dispatch.cmd_copy_buffer_to_image2_khr(commandBuffer, pCopyBufferToImageInfo); });
    }
}
#endif

void fill_layer_property(VkLayerProperties* p) {
    std::memset(p, 0, sizeof(*p));
    std::strncpy(p->layerName, kLayerName, sizeof(p->layerName) - 1);
    std::strncpy(p->description, "Vortek Xclipse BCn wrapper (native in-process)", sizeof(p->description) - 1);
    p->specVersion = VK_API_VERSION_1_3;
    p->implementationVersion = kLayerImplVersion;
}

VkResult enumerate_layer_props(uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pProperties) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }

    if (*pPropertyCount == 0) {
        return VK_INCOMPLETE;
    }

    fill_layer_property(&pProperties[0]);
    *pPropertyCount = 1;
    return VK_SUCCESS;
}

} // namespace

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return enumerate_layer_props(pPropertyCount, pProperties);
}

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return enumerate_layer_props(pPropertyCount, pProperties);
}

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    if (pLayerName && std::strcmp(pLayerName, kLayerName) != 0) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    InstanceDispatch dispatch{};
    VkInstance instance = VK_NULL_HANDLE;
    const bool can_forward =
        get_any_instance_dispatch(&dispatch, &instance) &&
        dispatch.get_instance_proc_addr &&
        instance != VK_NULL_HANDLE;

    if (can_forward && (!pLayerName || std::strcmp(pLayerName, kLayerName) == 0)) {
        auto enumerate_instance_extension_properties =
            reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
                dispatch.get_instance_proc_addr(instance, "vkEnumerateInstanceExtensionProperties"));
        if (enumerate_instance_extension_properties) {
            return enumerate_instance_extension_properties(nullptr, pPropertyCount, pProperties);
        }
    }

    uint32_t property_count = 0;
#ifdef VK_EXT_LAYER_SETTINGS_EXTENSION_NAME
    if (pLayerName && std::strcmp(pLayerName, kLayerName) == 0) {
        property_count = 1;
    }
#endif

    if (!pProperties) {
        *pPropertyCount = property_count;
        return VK_SUCCESS;
    }
    if (*pPropertyCount == 0) {
        return property_count == 0 ? VK_SUCCESS : VK_INCOMPLETE;
    }

#ifdef VK_EXT_LAYER_SETTINGS_EXTENSION_NAME
    if (property_count != 0) {
        std::memset(&pProperties[0], 0, sizeof(VkExtensionProperties));
        std::strncpy(
            pProperties[0].extensionName,
            VK_EXT_LAYER_SETTINGS_EXTENSION_NAME,
            sizeof(pProperties[0].extensionName) - 1);
        pProperties[0].specVersion = VK_EXT_LAYER_SETTINGS_SPEC_VERSION;
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }
#endif

    *pPropertyCount = 0;
    return VK_SUCCESS;
}

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    if (pLayerName && std::strcmp(pLayerName, kLayerName) != 0) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    InstanceDispatch dispatch{};
    VkInstance instance = VK_NULL_HANDLE;
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, &instance) ||
        !dispatch.get_instance_proc_addr ||
        instance == VK_NULL_HANDLE) {
        if (!get_any_instance_dispatch(&dispatch, &instance) ||
            !dispatch.get_instance_proc_addr ||
            instance == VK_NULL_HANDLE) {
            *pPropertyCount = 0;
            return VK_SUCCESS;
        }
    }

    auto enumerate_device_extension_properties =
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            dispatch.get_instance_proc_addr(instance, "vkEnumerateDeviceExtensionProperties"));
    if (!enumerate_device_extension_properties) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }

    uint32_t driver_property_count = 0;
    VkResult result = enumerate_device_extension_properties(
        physicalDevice,
        nullptr,
        &driver_property_count,
        nullptr);
    if (result != VK_SUCCESS || driver_property_count == 0) {
        *pPropertyCount = 0;
        return result;
    }

    std::vector<VkExtensionProperties> driver_properties(driver_property_count);
    uint32_t fetched_property_count = driver_property_count;
    result = enumerate_device_extension_properties(
        physicalDevice,
        nullptr,
        &fetched_property_count,
        driver_properties.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        *pPropertyCount = 0;
        return result;
    }
    driver_properties.resize(fetched_property_count);

    PhysicalRuntime runtime{};
    (void)get_physical_runtime_snapshot(physicalDevice, &runtime);

    std::vector<VkExtensionProperties> filtered_properties;
    filtered_properties.reserve(driver_properties.size());
    for (const VkExtensionProperties& property : driver_properties) {
        if (should_hide_device_extension(runtime, property.extensionName)) {
            continue;
        }
        filtered_properties.push_back(property);
    }

    if (!pProperties) {
        *pPropertyCount = static_cast<uint32_t>(filtered_properties.size());
        return VK_SUCCESS;
    }

    const uint32_t capacity = *pPropertyCount;
    const uint32_t write_count =
        std::min<uint32_t>(capacity, static_cast<uint32_t>(filtered_properties.size()));
    for (uint32_t i = 0; i < write_count; ++i) {
        pProperties[i] = filtered_properties[i];
    }
    *pPropertyCount = write_count;
    return write_count < filtered_properties.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName) {
    if (!pName) {
        return nullptr;
    }

    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceExtensionProperties);
    if (std::strcmp(pName, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateInstance);
    if (std::strcmp(pName, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyInstance);
    if (std::strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_EnumeratePhysicalDevices);
    if (std::strcmp(pName, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateDevice);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFeatures);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFeatures2);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFormatProperties);
    if (std::strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceImageFormatProperties);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFormatProperties2);
    if (std::strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceImageFormatProperties2);
#ifdef VK_KHR_get_physical_device_properties2
    if (std::strcmp(pName, "vkGetPhysicalDeviceFeatures2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFeatures2KHR);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFormatProperties2KHR);
    if (std::strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceImageFormatProperties2KHR);
#endif

    if (instance == VK_NULL_HANDLE) {
        return nullptr;
    }

    InstanceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_instance_dispatch.find(dispatch_key(instance));
        if (it == g_instance_dispatch.end()) {
            return nullptr;
        }
        dispatch = it->second;
    }

    if (!dispatch.get_instance_proc_addr) {
        return nullptr;
    }
    return dispatch.get_instance_proc_addr(instance, pName);
}

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char* pName) {
    if (!pName) {
        return nullptr;
    }

    auto should_intercept_copy_path = [&]() -> bool {
        const LayerSettingsSnapshot settings = snapshot_layer_settings();
        if (!settings.enabled || !settings.bcn_intercept) {
            return false;
        }
        if (device == VK_NULL_HANDLE) {
            return true;
        }
        if (!settings.xclipse_only) {
            return true;
        }
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_runtime.find(dispatch_key(device));
        if (it == g_device_runtime.end()) {
            return true;
        }
        return it->second.is_xclipse;
    };
    const bool intercept_copy_path = should_intercept_copy_path();

    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (std::strcmp(pName, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyDevice);
    if (std::strcmp(pName, "vkBindBufferMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_BindBufferMemory);
    if (std::strcmp(pName, "vkBindBufferMemory2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_BindBufferMemory2);
#ifdef VK_KHR_bind_memory2
    if (std::strcmp(pName, "vkBindBufferMemory2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_BindBufferMemory2KHR);
#endif
    if (std::strcmp(pName, "vkMapMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_MapMemory);
    if (std::strcmp(pName, "vkUnmapMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_UnmapMemory);
    if (std::strcmp(pName, "vkCreateGraphicsPipelines") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateGraphicsPipelines);
    if (std::strcmp(pName, "vkCreateImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateImage);
    if (std::strcmp(pName, "vkDestroyImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyImage);
    if (std::strcmp(pName, "vkCreateImageView") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateImageView);
    if (std::strcmp(pName, "vkCmdClearDepthStencilImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdClearDepthStencilImage);
    if (std::strcmp(pName, "vkCreateRenderPass") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateRenderPass);
    if (std::strcmp(pName, "vkCreateFramebuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateFramebuffer);
    if (std::strcmp(pName, "vkCreateSampler") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateSampler);
    if (std::strcmp(pName, "vkCreateCommandPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateCommandPool);
    if (std::strcmp(pName, "vkDestroyCommandPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyCommandPool);
    if (std::strcmp(pName, "vkResetCommandPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_ResetCommandPool);
    if (std::strcmp(pName, "vkBeginCommandBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_BeginCommandBuffer);
    if (std::strcmp(pName, "vkResetCommandBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_ResetCommandBuffer);
    if (std::strcmp(pName, "vkAllocateCommandBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_AllocateCommandBuffers);
    if (std::strcmp(pName, "vkFreeCommandBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_FreeCommandBuffers);
    if (std::strcmp(pName, "vkCmdCopyImage") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyImage);
    }
    if (std::strcmp(pName, "vkCmdCopyImage2") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyImage2);
    }
#ifdef VK_KHR_copy_commands2
    if (std::strcmp(pName, "vkCmdCopyImage2KHR") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyImage2KHR);
    }
#endif
    if (std::strcmp(pName, "vkCmdBlitImage") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdBlitImage);
    }
    if (std::strcmp(pName, "vkCmdBlitImage2") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdBlitImage2);
    }
#ifdef VK_KHR_copy_commands2
    if (std::strcmp(pName, "vkCmdBlitImage2KHR") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdBlitImage2KHR);
    }
#endif
    if (std::strcmp(pName, "vkCmdCopyBufferToImage") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyBufferToImage);
    }
    if (std::strcmp(pName, "vkCmdCopyBufferToImage2") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyBufferToImage2);
    }
#ifdef VK_KHR_copy_commands2
    if (std::strcmp(pName, "vkCmdCopyBufferToImage2KHR") == 0 && intercept_copy_path) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyBufferToImage2KHR);
    }
#endif

    if (device == VK_NULL_HANDLE) {
        return nullptr;
    }

    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return nullptr;
        }
        dispatch = it->second;
    }

    if (!dispatch.get_device_proc_addr) {
        return nullptr;
    }
    return dispatch.get_device_proc_addr(device, pName);
}

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* pVersionStruct) {
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pVersionStruct->loaderLayerInterfaceVersion > CURRENT_LOADER_LAYER_INTERFACE_VERSION) {
        pVersionStruct->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
    }

    pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    EXYNOS_LOGI("Layer negotiation complete. Interface version=%u", pVersionStruct->loaderLayerInterfaceVersion);
    return VK_SUCCESS;
}
