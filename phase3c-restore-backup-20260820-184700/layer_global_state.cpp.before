#include "layer_global_state.h"

std::shared_mutex g_lock;
std::unordered_map<void*, InstanceDispatch> g_instance_dispatch;
std::unordered_map<void*, InstanceRuntime> g_instance_runtime;
std::unordered_map<void*, DeviceDispatch> g_device_dispatch;
std::unordered_map<void*, void*> g_physical_to_instance;
std::unordered_map<void*, VkInstance> g_physical_to_instance_handle;
std::unordered_map<void*, PhysicalRuntime> g_physical_runtime;
std::unordered_map<void*, DeviceRuntime> g_device_runtime;
std::unordered_map<void*, VkInstance> g_device_to_instance_handle;
std::unordered_map<void*, VkPhysicalDevice> g_device_to_physical_handle;
std::unordered_map<void*, VirtualImageInfo> g_virtual_images;
std::unordered_map<void*, TrackedImageInfo> g_tracked_images;
std::unordered_map<void*, void*> g_image_to_device;
std::unordered_map<void*, TrackedBufferBinding> g_buffer_bindings;
std::unordered_map<void*, TrackedMemoryMap> g_memory_maps;
std::unordered_map<void*, std::shared_ptr<ComputeRuntime>> g_compute_runtime;
std::unordered_map<void*, std::shared_ptr<VmaRuntime>> g_vma_runtime;
std::unordered_map<StorageViewKey, VkImageView, StorageViewKeyHash> g_storage_views;
std::unordered_map<void*, DecodeImageState> g_decode_image_state;
std::unordered_map<BcnSupportKey, bool, BcnSupportKeyHash> g_bcn_native_support_cache;

std::atomic<bool> g_warned_missing_cmd_buffer_map{false};
std::atomic<bool> g_warned_cmd_buffer_dispatch_fallback{false};
std::atomic<bool> g_warned_cmd_buffer_dispatch_drop{false};

std::atomic<uint64_t> g_decode_attempts{0};
std::atomic<uint64_t> g_decode_successes{0};
std::atomic<uint64_t> g_decode_failures{0};
std::atomic<uint64_t> g_decode_passthrough_activations{0};
std::atomic<uint64_t> g_decode_feature_rejects{0};
std::atomic<uint64_t> g_decode_non2d_rejects{0};
std::atomic<uint64_t> g_decode_blocked_copies{0};
std::atomic<uint64_t> g_decode_retry_attempts{0};
std::atomic<uint64_t> g_decode_stats_log_gate{0};
std::atomic<uint64_t> g_microbenchmark_log_gate{0};
std::atomic<uint64_t> g_descriptor_pool_growths{0};
std::atomic<uint64_t> g_virtualized_create_images{0};
std::atomic<uint64_t> g_native_bcn_create_images{0};
std::atomic<uint64_t> g_virtualized_bcn_bc1{0};
std::atomic<uint64_t> g_virtualized_bcn_bc2{0};
std::atomic<uint64_t> g_virtualized_bcn_bc3{0};
std::atomic<uint64_t> g_virtualized_bcn_bc4{0};
std::atomic<uint64_t> g_virtualized_bcn_bc5{0};
std::atomic<uint64_t> g_virtualized_bcn_bc6{0};
std::atomic<uint64_t> g_virtualized_bcn_bc7{0};
std::atomic<uint64_t> g_virtualized_bcn_srgb{0};
std::atomic<uint64_t> g_decode_srgb_paths{0};
std::atomic<uint64_t> g_decode_3d_slices{0};
std::atomic<uint64_t> g_blocked_incompatible_virtual_transfers{0};
std::atomic<uint64_t> g_copy_image_calls{0};
std::atomic<uint64_t> g_copy_image_virtual_hits{0};
std::atomic<uint64_t> g_copy_image_real_routes{0};
std::atomic<uint64_t> g_copy_image_special_routes{0};
std::atomic<uint64_t> g_copy_image_special_fallbacks{0};
std::atomic<uint64_t> g_wave32_pipeline_tries{0};
std::atomic<uint64_t> g_wave64_pipeline_tries{0};

LayerSettingsState& layer_settings_state() {
    static LayerSettingsState settings;
    return settings;
}
