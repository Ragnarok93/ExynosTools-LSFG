#include "layer_command_buffer_resources.h"

#include <mutex>
#include <unordered_map>
#include <utility>

#include "layer_dispatch_key.h"

namespace {

std::unordered_map<void*, std::vector<StagingAllocation>>& command_buffer_staging_allocations() {
    static std::unordered_map<void*, std::vector<StagingAllocation>> allocations;
    return allocations;
}

std::vector<std::vector<StagingAllocation>>& recycled_staging_allocation_vectors() {
    static std::vector<std::vector<StagingAllocation>> recycled;
    return recycled;
}

std::unordered_map<void*, std::vector<TrackedDescriptorSet>>& command_buffer_descriptor_sets() {
    static std::unordered_map<void*, std::vector<TrackedDescriptorSet>> descriptor_sets;
    return descriptor_sets;
}

std::vector<std::vector<TrackedDescriptorSet>>& recycled_descriptor_set_vectors() {
    static std::vector<std::vector<TrackedDescriptorSet>> recycled;
    return recycled;
}

std::mutex& command_buffer_resource_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<StagingAllocation>& acquire_staging_allocation_vector(void* command_buffer_key) {
    auto& allocations = command_buffer_staging_allocations();
    auto it = allocations.find(command_buffer_key);
    if (it != allocations.end()) {
        return it->second;
    }

    auto& recycled = recycled_staging_allocation_vectors();
    if (!recycled.empty()) {
        allocations.emplace(command_buffer_key, std::move(recycled.back()));
        recycled.pop_back();
    } else {
        allocations.emplace(command_buffer_key, std::vector<StagingAllocation>{});
    }
    return allocations.find(command_buffer_key)->second;
}

std::vector<TrackedDescriptorSet>& acquire_descriptor_set_vector(void* command_buffer_key) {
    auto& descriptor_sets = command_buffer_descriptor_sets();
    auto it = descriptor_sets.find(command_buffer_key);
    if (it != descriptor_sets.end()) {
        return it->second;
    }

    auto& recycled = recycled_descriptor_set_vectors();
    if (!recycled.empty()) {
        descriptor_sets.emplace(command_buffer_key, std::move(recycled.back()));
        recycled.pop_back();
    } else {
        descriptor_sets.emplace(command_buffer_key, std::vector<TrackedDescriptorSet>{});
    }
    return descriptor_sets.find(command_buffer_key)->second;
}

}  // namespace

void track_command_buffer_staging_allocation(
    VkCommandBuffer command_buffer,
    StagingAllocation&& staging) {
    std::lock_guard<std::mutex> guard(command_buffer_resource_mutex());
    acquire_staging_allocation_vector(dispatch_key(command_buffer)).push_back(std::move(staging));
}

void take_command_buffer_staging_allocations(
    void* command_buffer_key,
    std::vector<StagingAllocation>* out_allocations) {
    if (!out_allocations) {
        return;
    }

    std::lock_guard<std::mutex> guard(command_buffer_resource_mutex());
    auto& allocations = command_buffer_staging_allocations();
    auto it = allocations.find(command_buffer_key);
    if (it == allocations.end()) {
        return;
    }

    auto& src = it->second;
    out_allocations->insert(
        out_allocations->end(),
        std::make_move_iterator(src.begin()),
        std::make_move_iterator(src.end()));
    src.clear();
    recycled_staging_allocation_vectors().push_back(std::move(src));
    allocations.erase(it);
}

void track_command_buffer_descriptor_set(
    VkCommandBuffer command_buffer,
    VkDescriptorPool descriptor_pool,
    VkDescriptorSet descriptor_set) {
    if (descriptor_pool == VK_NULL_HANDLE || descriptor_set == VK_NULL_HANDLE) {
        return;
    }

    std::lock_guard<std::mutex> guard(command_buffer_resource_mutex());
    acquire_descriptor_set_vector(dispatch_key(command_buffer)).push_back(
        TrackedDescriptorSet{descriptor_pool, descriptor_set});
}

void take_command_buffer_descriptor_sets(
    void* command_buffer_key,
    std::vector<TrackedDescriptorSet>* out_sets) {
    if (!out_sets) {
        return;
    }

    std::lock_guard<std::mutex> guard(command_buffer_resource_mutex());
    auto& descriptor_sets = command_buffer_descriptor_sets();
    auto it = descriptor_sets.find(command_buffer_key);
    if (it == descriptor_sets.end()) {
        return;
    }

    auto& src = it->second;
    out_sets->insert(
        out_sets->end(),
        std::make_move_iterator(src.begin()),
        std::make_move_iterator(src.end()));
    src.clear();
    recycled_descriptor_set_vectors().push_back(std::move(src));
    descriptor_sets.erase(it);
}
