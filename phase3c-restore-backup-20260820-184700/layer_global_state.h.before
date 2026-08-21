#pragma once

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "layer_compute_runtime.h"
#include "layer_device_dispatch_types.h"
#include "layer_dispatch_types.h"
#include "layer_format_virtualization.h"
#include "layer_settings_types.h"
#include "layer_shared_types.h"
#include "layer_vma_runtime.h"

extern std::shared_mutex g_lock;
extern std::unordered_map<void*, InstanceDispatch> g_instance_dispatch;
extern std::unordered_map<void*, InstanceRuntime> g_instance_runtime;
extern std::unordered_map<void*, DeviceDispatch> g_device_dispatch;
extern std::unordered_map<void*, void*> g_physical_to_instance;
extern std::unordered_map<void*, VkInstance> g_physical_to_instance_handle;
extern std::unordered_map<void*, PhysicalRuntime> g_physical_runtime;
extern std::unordered_map<void*, DeviceRuntime> g_device_runtime;
extern std::unordered_map<void*, VkInstance> g_device_to_instance_handle;
extern std::unordered_map<void*, VkPhysicalDevice> g_device_to_physical_handle;
extern std::unordered_map<void*, VirtualImageInfo> g_virtual_images;
extern std::unordered_map<void*, TrackedImageInfo> g_tracked_images;
extern std::unordered_map<void*, void*> g_image_to_device;
extern std::unordered_map<void*, TrackedBufferBinding> g_buffer_bindings;
extern std::unordered_map<void*, TrackedMemoryMap> g_memory_maps;
extern std::unordered_map<void*, std::shared_ptr<ComputeRuntime>> g_compute_runtime;
extern std::unordered_map<void*, std::shared_ptr<VmaRuntime>> g_vma_runtime;
extern std::unordered_map<StorageViewKey, VkImageView, StorageViewKeyHash> g_storage_views;
extern std::unordered_map<void*, DecodeImageState> g_decode_image_state;
extern std::unordered_map<BcnSupportKey, bool, BcnSupportKeyHash> g_bcn_native_support_cache;

extern std::atomic<bool> g_warned_missing_cmd_buffer_map;
extern std::atomic<bool> g_warned_cmd_buffer_dispatch_fallback;
extern std::atomic<bool> g_warned_cmd_buffer_dispatch_drop;

extern std::atomic<uint64_t> g_decode_attempts;
extern std::atomic<uint64_t> g_decode_successes;
extern std::atomic<uint64_t> g_decode_failures;
extern std::atomic<uint64_t> g_decode_passthrough_activations;
extern std::atomic<uint64_t> g_decode_feature_rejects;
extern std::atomic<uint64_t> g_decode_non2d_rejects;
extern std::atomic<uint64_t> g_decode_blocked_copies;
extern std::atomic<uint64_t> g_decode_retry_attempts;
extern std::atomic<uint64_t> g_decode_stats_log_gate;
extern std::atomic<uint64_t> g_microbenchmark_log_gate;
extern std::atomic<uint64_t> g_descriptor_pool_growths;
extern std::atomic<uint64_t> g_virtualized_create_images;
extern std::atomic<uint64_t> g_native_bcn_create_images;
extern std::atomic<uint64_t> g_virtualized_bcn_bc1;
extern std::atomic<uint64_t> g_virtualized_bcn_bc2;
extern std::atomic<uint64_t> g_virtualized_bcn_bc3;
extern std::atomic<uint64_t> g_virtualized_bcn_bc4;
extern std::atomic<uint64_t> g_virtualized_bcn_bc5;
extern std::atomic<uint64_t> g_virtualized_bcn_bc6;
extern std::atomic<uint64_t> g_virtualized_bcn_bc7;
extern std::atomic<uint64_t> g_virtualized_bcn_srgb;
extern std::atomic<uint64_t> g_decode_srgb_paths;
extern std::atomic<uint64_t> g_decode_3d_slices;
extern std::atomic<uint64_t> g_blocked_incompatible_virtual_transfers;
extern std::atomic<uint64_t> g_copy_image_calls;
extern std::atomic<uint64_t> g_copy_image_virtual_hits;
extern std::atomic<uint64_t> g_copy_image_real_routes;
extern std::atomic<uint64_t> g_copy_image_special_routes;
extern std::atomic<uint64_t> g_copy_image_special_fallbacks;
extern std::atomic<uint64_t> g_wave32_pipeline_tries;
extern std::atomic<uint64_t> g_wave64_pipeline_tries;

LayerSettingsState& layer_settings_state();
