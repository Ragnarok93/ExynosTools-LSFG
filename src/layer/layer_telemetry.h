#pragma once

#include <cstdint>

#include "layer_pipeline_selection.h"

void maybe_log_decode_stats();

enum class BenchmarkDomain : uint32_t {
    DecodeCpu = 0,
    SpecialCopyCpu = 1,
    DecodeGpu = 2,
    SpecialCopyGpu = 3,
};

void record_microbenchmark_sample(
    BenchmarkDomain domain,
    DecoderShaderKind shader_kind,
    uint64_t duration_ns,
    uint32_t work_items,
    bool success);

struct DeviceDispatch;

bool begin_gpu_microbenchmark(
    VkDevice device,
    const DeviceDispatch& dispatch,
    float timestamp_period,
    VkCommandBuffer command_buffer,
    BenchmarkDomain domain,
    DecoderShaderKind shader_kind,
    uint32_t work_items,
    VkQueryPool* out_query_pool);

void end_gpu_microbenchmark(
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkCommandBuffer command_buffer,
    VkQueryPool query_pool);

void collect_gpu_microbenchmarks(
    VkDevice device,
    const DeviceDispatch& dispatch,
    bool destroy_unready);
