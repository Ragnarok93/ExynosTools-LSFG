#include "layer_telemetry.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "layer_device_dispatch_types.h"
#include "layer_dispatch_key.h"
#include "layer_global_state.h"
#include "layer_logging.h"
#include "layer_settings_runtime.h"

namespace {

constexpr uint64_t kMicrobenchmarkLogInterval = 64u;
constexpr size_t kBenchmarkShaderBucketCount =
    static_cast<size_t>(DecoderShaderKind::CopyImageRgba16f) + 1u;
constexpr size_t kBenchmarkDomainCount = 4u;

struct MicrobenchmarkBucket {
    uint64_t samples = 0;
    uint64_t successes = 0;
    uint64_t total_duration_ns = 0;
    uint64_t max_duration_ns = 0;
    uint64_t total_work_items = 0;
};

struct PendingGpuBenchmarkQuery {
    VkQueryPool query_pool = VK_NULL_HANDLE;
    BenchmarkDomain domain = BenchmarkDomain::DecodeGpu;
    DecoderShaderKind shader_kind = DecoderShaderKind::None;
    uint32_t work_items = 0;
    float timestamp_period = 0.0f;
};

struct CompletedGpuBenchmarkSample {
    BenchmarkDomain domain = BenchmarkDomain::DecodeGpu;
    DecoderShaderKind shader_kind = DecoderShaderKind::None;
    uint32_t work_items = 0;
    uint64_t ticks = 0;
    float timestamp_period = 0.0f;
};

std::mutex& microbenchmark_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::array<std::array<MicrobenchmarkBucket, kBenchmarkShaderBucketCount>, kBenchmarkDomainCount>&
microbenchmark_buckets() {
    static std::array<std::array<MicrobenchmarkBucket, kBenchmarkShaderBucketCount>, kBenchmarkDomainCount> buckets{};
    return buckets;
}

std::unordered_map<void*, std::vector<PendingGpuBenchmarkQuery>>& pending_gpu_benchmarks() {
    static std::unordered_map<void*, std::vector<PendingGpuBenchmarkQuery>> queries;
    return queries;
}

std::unordered_map<void*, std::vector<VkQueryPool>>& recycled_gpu_query_pools() {
    static std::unordered_map<void*, std::vector<VkQueryPool>> pools;
    return pools;
}

const char* benchmark_domain_name(BenchmarkDomain domain) {
    switch (domain) {
        case BenchmarkDomain::DecodeCpu:
            return "decode_cpu";
        case BenchmarkDomain::SpecialCopyCpu:
            return "copy_cpu";
        case BenchmarkDomain::DecodeGpu:
            return "decode_gpu";
        case BenchmarkDomain::SpecialCopyGpu:
            return "copy_gpu";
        default:
            return "unknown";
    }
}

const char* shader_kind_name(DecoderShaderKind kind) {
    switch (kind) {
        case DecoderShaderKind::S3tc: return "s3tc";
        case DecoderShaderKind::RgtcR8Unorm: return "rgtc_r8_unorm";
        case DecoderShaderKind::RgtcR8Snorm: return "rgtc_r8_snorm";
        case DecoderShaderKind::RgtcRg8Unorm: return "rgtc_rg8_unorm";
        case DecoderShaderKind::RgtcRg8Snorm: return "rgtc_rg8_snorm";
        case DecoderShaderKind::RgtcRgba8Unorm: return "rgtc_rgba8_unorm";
        case DecoderShaderKind::RgtcRgba8Snorm: return "rgtc_rgba8_snorm";
        case DecoderShaderKind::Bc6: return "bc6";
        case DecoderShaderKind::Bc7: return "bc7";
        case DecoderShaderKind::CopyImageR8Unorm: return "copy_r8_unorm";
        case DecoderShaderKind::CopyImageR8Snorm: return "copy_r8_snorm";
        case DecoderShaderKind::CopyImageRg8Unorm: return "copy_rg8_unorm";
        case DecoderShaderKind::CopyImageRg8Snorm: return "copy_rg8_snorm";
        case DecoderShaderKind::CopyImageRgba8Unorm: return "copy_rgba8_unorm";
        case DecoderShaderKind::CopyImageRgba8Snorm: return "copy_rgba8_snorm";
        case DecoderShaderKind::CopyImageRgba16f: return "copy_rgba16f";
        case DecoderShaderKind::None:
        default:
            return "none";
    }
}

void maybe_log_microbenchmark_stats_locked() {
    uint64_t sample = g_microbenchmark_log_gate.load(std::memory_order_relaxed);
    if ((sample % kMicrobenchmarkLogInterval) != 0u) {
        return;
    }

    auto& buckets = microbenchmark_buckets();
    for (size_t domain_index = 0; domain_index < buckets.size(); ++domain_index) {
        for (size_t shader_index = 0; shader_index < buckets[domain_index].size(); ++shader_index) {
            const MicrobenchmarkBucket& bucket = buckets[domain_index][shader_index];
            if (bucket.samples == 0) {
                continue;
            }

            const uint64_t avg_ns = bucket.total_duration_ns / bucket.samples;
            const uint64_t avg_work_items = bucket.total_work_items / bucket.samples;
            EXYNOS_LOGI(
                "Microbench %s/%s: samples=%llu ok=%llu avg_us=%llu max_us=%llu avg_work_items=%llu",
                benchmark_domain_name(static_cast<BenchmarkDomain>(domain_index)),
                shader_kind_name(static_cast<DecoderShaderKind>(shader_index)),
                static_cast<unsigned long long>(bucket.samples),
                static_cast<unsigned long long>(bucket.successes),
                static_cast<unsigned long long>(avg_ns / 1000u),
                static_cast<unsigned long long>(bucket.max_duration_ns / 1000u),
                static_cast<unsigned long long>(avg_work_items));
        }
    }
}

VkQueryPool acquire_gpu_benchmark_query_pool(
    VkDevice device,
    const DeviceDispatch& dispatch) {
    void* device_key = dispatch_key(device);
    {
        std::lock_guard<std::mutex> guard(microbenchmark_mutex());
        auto& recycled = recycled_gpu_query_pools();
        auto it = recycled.find(device_key);
        if (it != recycled.end() && !it->second.empty()) {
            VkQueryPool query_pool = it->second.back();
            it->second.pop_back();
            if (it->second.empty()) {
                recycled.erase(it);
            }
            if (query_pool != VK_NULL_HANDLE) {
                return query_pool;
            }
        }
    }

    VkQueryPoolCreateInfo query_ci{};
    query_ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_ci.queryCount = 2;

    VkQueryPool query_pool = VK_NULL_HANDLE;
    if (dispatch.create_query_pool(device, &query_ci, nullptr, &query_pool) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return query_pool;
}

void recycle_gpu_benchmark_query_pool(VkDevice device, VkQueryPool query_pool) {
    if (device == VK_NULL_HANDLE || query_pool == VK_NULL_HANDLE) {
        return;
    }
    std::lock_guard<std::mutex> guard(microbenchmark_mutex());
    recycled_gpu_query_pools()[dispatch_key(device)].push_back(query_pool);
}

void destroy_gpu_benchmark_query_pool(
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkQueryPool query_pool) {
    if (device == VK_NULL_HANDLE || query_pool == VK_NULL_HANDLE || !dispatch.destroy_query_pool) {
        return;
    }
    dispatch.destroy_query_pool(device, query_pool, nullptr);
}

void destroy_recycled_gpu_benchmark_query_pools(
    VkDevice device,
    const DeviceDispatch& dispatch) {
    if (device == VK_NULL_HANDLE || !dispatch.destroy_query_pool) {
        return;
    }

    std::vector<VkQueryPool> pools_to_destroy;
    {
        std::lock_guard<std::mutex> guard(microbenchmark_mutex());
        auto& recycled = recycled_gpu_query_pools();
        auto it = recycled.find(dispatch_key(device));
        if (it == recycled.end()) {
            return;
        }
        pools_to_destroy = std::move(it->second);
        recycled.erase(it);
    }

    for (VkQueryPool query_pool : pools_to_destroy) {
        destroy_gpu_benchmark_query_pool(device, dispatch, query_pool);
    }
}

}  // namespace

void maybe_log_decode_stats() {
    uint64_t sample = g_decode_stats_log_gate.fetch_add(1) + 1;
    if ((sample % 256u) != 0u) {
        return;
    }
    EXYNOS_LOGI(
        "BCn stats: attempts=%llu success=%llu fail=%llu passthrough=%llu featureReject=%llu non2D=%llu blockedCopies=%llu retries=%llu virtualizedCreates=%llu nativeCreates=%llu fmt[bc1=%llu bc2=%llu bc3=%llu bc4=%llu bc5=%llu bc6=%llu bc7=%llu srgb=%llu] decode[srgb=%llu slices3D=%llu] blockedTransfers=%llu poolGrows=%llu copyImageCalls=%llu copyImageVirtual=%llu copyImageHandled=%llu wave32Tries=%llu wave64Tries=%llu",
        static_cast<unsigned long long>(g_decode_attempts.load()),
        static_cast<unsigned long long>(g_decode_successes.load()),
        static_cast<unsigned long long>(g_decode_failures.load()),
        static_cast<unsigned long long>(g_decode_passthrough_activations.load()),
        static_cast<unsigned long long>(g_decode_feature_rejects.load()),
        static_cast<unsigned long long>(g_decode_non2d_rejects.load()),
        static_cast<unsigned long long>(g_decode_blocked_copies.load()),
        static_cast<unsigned long long>(g_decode_retry_attempts.load()),
        static_cast<unsigned long long>(g_virtualized_create_images.load()),
        static_cast<unsigned long long>(g_native_bcn_create_images.load()),
        static_cast<unsigned long long>(g_virtualized_bcn_bc1.load()),
        static_cast<unsigned long long>(g_virtualized_bcn_bc2.load()),
        static_cast<unsigned long long>(g_virtualized_bcn_bc3.load()),
        static_cast<unsigned long long>(g_virtualized_bcn_bc4.load()),
        static_cast<unsigned long long>(g_virtualized_bcn_bc5.load()),
        static_cast<unsigned long long>(g_virtualized_bcn_bc6.load()),
        static_cast<unsigned long long>(g_virtualized_bcn_bc7.load()),
        static_cast<unsigned long long>(g_virtualized_bcn_srgb.load()),
        static_cast<unsigned long long>(g_decode_srgb_paths.load()),
        static_cast<unsigned long long>(g_decode_3d_slices.load()),
        static_cast<unsigned long long>(g_blocked_incompatible_virtual_transfers.load()),
        static_cast<unsigned long long>(g_descriptor_pool_growths.load()),
        static_cast<unsigned long long>(g_copy_image_calls.load()),
        static_cast<unsigned long long>(g_copy_image_virtual_hits.load()),
        static_cast<unsigned long long>(g_copy_image_real_routes.load() + g_copy_image_special_routes.load()),
        static_cast<unsigned long long>(g_wave32_pipeline_tries.load()),
        static_cast<unsigned long long>(g_wave64_pipeline_tries.load()));
}

void record_microbenchmark_sample(
    BenchmarkDomain domain,
    DecoderShaderKind shader_kind,
    uint64_t duration_ns,
    uint32_t work_items,
    bool success) {
    if (shader_kind == DecoderShaderKind::None) {
        return;
    }

    LayerSettingsSnapshot settings = snapshot_layer_settings();
    if (!settings.microbenchmark_enabled) {
        return;
    }

    const size_t domain_index = static_cast<size_t>(domain);
    const size_t shader_index = static_cast<size_t>(shader_kind);
    if (domain_index >= kBenchmarkDomainCount || shader_index >= kBenchmarkShaderBucketCount) {
        return;
    }

    std::lock_guard<std::mutex> guard(microbenchmark_mutex());
    MicrobenchmarkBucket& bucket = microbenchmark_buckets()[domain_index][shader_index];
    bucket.samples += 1;
    bucket.successes += success ? 1u : 0u;
    bucket.total_duration_ns += duration_ns;
    bucket.max_duration_ns = std::max(bucket.max_duration_ns, duration_ns);
    bucket.total_work_items += work_items;
    g_microbenchmark_log_gate.fetch_add(1, std::memory_order_relaxed);
    maybe_log_microbenchmark_stats_locked();
}

bool begin_gpu_microbenchmark(
    VkDevice device,
    const DeviceDispatch& dispatch,
    float timestamp_period,
    VkCommandBuffer command_buffer,
    BenchmarkDomain domain,
    DecoderShaderKind shader_kind,
    uint32_t work_items,
    VkQueryPool* out_query_pool) {
    if (out_query_pool) {
        *out_query_pool = VK_NULL_HANDLE;
    }

    LayerSettingsSnapshot settings = snapshot_layer_settings();
    if (!settings.microbenchmark_enabled ||
        device == VK_NULL_HANDLE ||
        command_buffer == VK_NULL_HANDLE ||
        shader_kind == DecoderShaderKind::None ||
        timestamp_period <= 0.0f ||
        !dispatch.create_query_pool ||
        !dispatch.destroy_query_pool ||
        !dispatch.get_query_pool_results ||
        !dispatch.cmd_reset_query_pool ||
        !dispatch.cmd_write_timestamp ||
        !out_query_pool) {
        return false;
    }

    VkQueryPool query_pool = acquire_gpu_benchmark_query_pool(device, dispatch);
    if (query_pool == VK_NULL_HANDLE) {
        return false;
    }

    dispatch.cmd_reset_query_pool(command_buffer, query_pool, 0, 2);
    dispatch.cmd_write_timestamp(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, query_pool, 0);

    {
        std::lock_guard<std::mutex> guard(microbenchmark_mutex());
        pending_gpu_benchmarks()[dispatch_key(device)].push_back(
            PendingGpuBenchmarkQuery{query_pool, domain, shader_kind, work_items, timestamp_period});
    }
    *out_query_pool = query_pool;
    return true;
}

void end_gpu_microbenchmark(
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkCommandBuffer command_buffer,
    VkQueryPool query_pool) {
    (void)device;
    if (command_buffer == VK_NULL_HANDLE ||
        query_pool == VK_NULL_HANDLE ||
        !dispatch.cmd_write_timestamp) {
        return;
    }

    dispatch.cmd_write_timestamp(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, query_pool, 1);
}

void collect_gpu_microbenchmarks(
    VkDevice device,
    const DeviceDispatch& dispatch,
    bool destroy_unready) {
    if (device == VK_NULL_HANDLE ||
        !dispatch.get_query_pool_results ||
        !dispatch.destroy_query_pool) {
        return;
    }

    std::vector<CompletedGpuBenchmarkSample> ready_queries;
    std::vector<PendingGpuBenchmarkQuery> queries_to_destroy;
    {
        std::lock_guard<std::mutex> guard(microbenchmark_mutex());
        auto& pending = pending_gpu_benchmarks();
        auto it = pending.find(dispatch_key(device));
        if (it == pending.end()) {
            return;
        }

        std::vector<PendingGpuBenchmarkQuery> remaining;
        remaining.reserve(it->second.size());
        for (const PendingGpuBenchmarkQuery& query : it->second) {
            uint64_t results[4] = {};
            VkResult result = dispatch.get_query_pool_results(
                device,
                query.query_pool,
                0,
                2,
                sizeof(results),
                results,
                sizeof(uint64_t) * 2,
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

            const bool ready =
                result == VK_SUCCESS &&
                results[1] != 0 &&
                results[3] != 0 &&
                results[2] >= results[0];
            if (ready) {
                const uint64_t ticks = results[2] - results[0];
                ready_queries.push_back(
                    CompletedGpuBenchmarkSample{
                        query.domain,
                        query.shader_kind,
                        query.work_items,
                        ticks,
                        query.timestamp_period});
                queries_to_destroy.push_back(query);
                continue;
            }

            if (destroy_unready) {
                queries_to_destroy.push_back(query);
                continue;
            }
            remaining.push_back(query);
        }

        if (remaining.empty()) {
            pending.erase(it);
        } else {
            it->second = std::move(remaining);
        }
    }

    for (const CompletedGpuBenchmarkSample& query : ready_queries) {
        const uint64_t duration_ns = static_cast<uint64_t>(
            static_cast<double>(query.ticks) * static_cast<double>(query.timestamp_period));
        record_microbenchmark_sample(
            query.domain,
            query.shader_kind,
            duration_ns,
            query.work_items,
            true);
    }

    for (const PendingGpuBenchmarkQuery& query : queries_to_destroy) {
        if (destroy_unready) {
            destroy_gpu_benchmark_query_pool(device, dispatch, query.query_pool);
        } else {
            recycle_gpu_benchmark_query_pool(device, query.query_pool);
        }
    }

    if (destroy_unready) {
        destroy_recycled_gpu_benchmark_query_pools(device, dispatch);
    }
}
