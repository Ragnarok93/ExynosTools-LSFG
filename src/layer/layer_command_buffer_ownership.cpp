#include "layer_command_buffer_ownership.h"

#include <mutex>
#include <unordered_map>

#include "layer_dispatch_key.h"

namespace {

std::unordered_map<void*, void*>& command_buffer_to_device() {
    static std::unordered_map<void*, void*> map;
    return map;
}

std::unordered_map<void*, VkDevice>& command_buffer_device_handle() {
    static std::unordered_map<void*, VkDevice> map;
    return map;
}

std::unordered_map<void*, void*>& command_buffer_to_pool() {
    static std::unordered_map<void*, void*> map;
    return map;
}

std::unordered_map<void*, void*>& command_pool_to_device() {
    static std::unordered_map<void*, void*> map;
    return map;
}

std::mutex& command_buffer_ownership_mutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

void track_command_pool_device(VkCommandPool command_pool, VkDevice device) {
    std::lock_guard<std::mutex> guard(command_buffer_ownership_mutex());
    command_pool_to_device()[dispatch_key(command_pool)] = dispatch_key(device);
}

void track_allocated_command_buffers(
    VkDevice device,
    VkCommandPool command_pool,
    uint32_t command_buffer_count,
    const VkCommandBuffer* command_buffers) {
    if (!command_buffers) {
        return;
    }

    std::lock_guard<std::mutex> guard(command_buffer_ownership_mutex());
    void* device_key = dispatch_key(device);
    void* command_pool_key = dispatch_key(command_pool);
    command_pool_to_device()[command_pool_key] = device_key;
    for (uint32_t i = 0; i < command_buffer_count; ++i) {
        void* command_buffer_key = dispatch_key(command_buffers[i]);
        command_buffer_to_device()[command_buffer_key] = device_key;
        command_buffer_device_handle()[command_buffer_key] = device;
        command_buffer_to_pool()[command_buffer_key] = command_pool_key;
    }
}

void collect_command_buffers_for_device(
    void* device_key,
    std::vector<void*>* out_command_buffer_keys) {
    if (!out_command_buffer_keys) {
        return;
    }

    std::lock_guard<std::mutex> guard(command_buffer_ownership_mutex());
    for (auto it = command_buffer_to_device().begin(); it != command_buffer_to_device().end();) {
        if (it->second == device_key) {
            out_command_buffer_keys->push_back(it->first);
            command_buffer_to_pool().erase(it->first);
            command_buffer_device_handle().erase(it->first);
            it = command_buffer_to_device().erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = command_pool_to_device().begin(); it != command_pool_to_device().end();) {
        if (it->second == device_key) {
            it = command_pool_to_device().erase(it);
        } else {
            ++it;
        }
    }
}

void collect_command_buffers_for_pool(
    void* command_pool_key,
    std::vector<void*>* out_command_buffer_keys) {
    if (!out_command_buffer_keys) {
        return;
    }

    std::lock_guard<std::mutex> guard(command_buffer_ownership_mutex());
    for (auto it = command_buffer_to_pool().begin(); it != command_buffer_to_pool().end();) {
        if (it->second == command_pool_key) {
            out_command_buffer_keys->push_back(it->first);
            command_buffer_to_device().erase(it->first);
            command_buffer_device_handle().erase(it->first);
            it = command_buffer_to_pool().erase(it);
        } else {
            ++it;
        }
    }
}

void list_command_buffers_for_pool(
    void* command_pool_key,
    std::vector<void*>* out_command_buffer_keys) {
    if (!out_command_buffer_keys) {
        return;
    }

    std::lock_guard<std::mutex> guard(command_buffer_ownership_mutex());
    for (const auto& it : command_buffer_to_pool()) {
        if (it.second == command_pool_key) {
            out_command_buffer_keys->push_back(it.first);
        }
    }
}

void erase_command_pool_tracking(void* command_pool_key) {
    std::lock_guard<std::mutex> guard(command_buffer_ownership_mutex());
    command_pool_to_device().erase(command_pool_key);
}

void erase_command_buffer_tracking(void* command_buffer_key) {
    std::lock_guard<std::mutex> guard(command_buffer_ownership_mutex());
    command_buffer_to_device().erase(command_buffer_key);
    command_buffer_device_handle().erase(command_buffer_key);
    command_buffer_to_pool().erase(command_buffer_key);
}

bool get_command_buffer_device_mapping(
    VkCommandBuffer command_buffer,
    void** out_device_key,
    VkDevice* out_device) {
    if (!out_device_key || !out_device) {
        return false;
    }

    *out_device_key = nullptr;
    *out_device = VK_NULL_HANDLE;

    std::lock_guard<std::mutex> guard(command_buffer_ownership_mutex());
    void* command_buffer_key = dispatch_key(command_buffer);
    auto device_it = command_buffer_to_device().find(command_buffer_key);
    if (device_it == command_buffer_to_device().end()) {
        return false;
    }

    *out_device_key = device_it->second;
    auto handle_it = command_buffer_device_handle().find(command_buffer_key);
    if (handle_it != command_buffer_device_handle().end()) {
        *out_device = handle_it->second;
    }
    return true;
}
