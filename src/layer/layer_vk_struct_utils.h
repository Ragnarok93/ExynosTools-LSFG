#pragma once

#include <array>

#include <vulkan/vulkan.h>
#include <vulkan/utility/vk_safe_struct_utils.hpp>

template <typename T>
T* find_struct_in_pnext_chain(
    void* next,
    VkStructureType sType,
    VkBaseOutStructure** out_previous = nullptr) {
    VkBaseOutStructure* previous = nullptr;
    auto* current = reinterpret_cast<VkBaseOutStructure*>(next);
    while (current) {
        if (current->sType == sType) {
            if (out_previous) {
                *out_previous = previous;
            }
            return reinterpret_cast<T*>(current);
        }
        previous = current;
        current = const_cast<VkBaseOutStructure*>(current->pNext);
    }
    if (out_previous) {
        *out_previous = nullptr;
    }
    return nullptr;
}

template <typename T>
const T* find_struct_in_pnext_chain(
    const void* next,
    VkStructureType sType) {
    auto* current = reinterpret_cast<const VkBaseInStructure*>(next);
    while (current) {
        if (current->sType == sType) {
            return reinterpret_cast<const T*>(current);
        }
        current = current->pNext;
    }
    return nullptr;
}

template <typename T>
bool has_struct_in_pnext_chain(
    const void* next,
    VkStructureType sType) {
    return find_struct_in_pnext_chain<T>(next, sType) != nullptr;
}

template <typename T>
void append_struct_to_pnext_chain(const void** io_head, T* node) {
    if (!io_head || !node) {
        return;
    }
    node->pNext = nullptr;
    auto** previous_next = const_cast<VkBaseOutStructure**>(
        reinterpret_cast<const VkBaseOutStructure* const*>(io_head));
    auto* current = *previous_next;
    while (current) {
        previous_next = &current->pNext;
        current = current->pNext;
    }
    *previous_next = reinterpret_cast<VkBaseOutStructure*>(node);
}

template <typename T>
void prepend_struct_to_pnext_chain(const void** io_head, T* node) {
    if (!io_head || !node) {
        return;
    }
    node->pNext = *io_head;
    *io_head = node;
}

template <typename T>
bool remove_struct_from_pnext_chain(
    const void** io_head,
    VkStructureType sType,
    T** out_removed = nullptr) {
    if (!io_head) {
        if (out_removed) {
            *out_removed = nullptr;
        }
        return false;
    }

    VkBaseOutStructure* previous = nullptr;
    auto* current = reinterpret_cast<VkBaseOutStructure*>(const_cast<void*>(*io_head));
    while (current) {
        if (current->sType == sType) {
            const void* next = current->pNext;
            if (previous) {
                previous->pNext = reinterpret_cast<VkBaseOutStructure*>(const_cast<void*>(next));
            } else {
                *io_head = next;
            }
            current->pNext = nullptr;
            if (out_removed) {
                *out_removed = reinterpret_cast<T*>(current);
            }
            return true;
        }
        previous = current;
        current = current->pNext;
    }

    if (out_removed) {
        *out_removed = nullptr;
    }
    return false;
}

template <typename T>
bool remove_struct_from_cloned_pnext_chain(
    const void** io_head,
    VkStructureType sType) {
    T* removed = nullptr;
    if (!remove_struct_from_pnext_chain(io_head, sType, &removed)) {
        return false;
    }
    if (removed) {
        vku::FreePnextChain(removed);
    }
    return true;
}

template <typename T>
bool replace_struct_in_pnext_chain(
    const void** io_head,
    VkStructureType sType,
    T* replacement,
    T** out_replaced = nullptr) {
    if (!io_head || !replacement) {
        if (out_replaced) {
            *out_replaced = nullptr;
        }
        return false;
    }

    VkBaseOutStructure* previous = nullptr;
    auto* current = reinterpret_cast<VkBaseOutStructure*>(const_cast<void*>(*io_head));
    while (current) {
        if (current->sType == sType) {
            replacement->pNext = current->pNext;
            if (previous) {
                previous->pNext = reinterpret_cast<VkBaseOutStructure*>(replacement);
            } else {
                *io_head = replacement;
            }
            current->pNext = nullptr;
            if (out_replaced) {
                *out_replaced = reinterpret_cast<T*>(current);
            }
            return true;
        }
        previous = current;
        current = current->pNext;
    }

    if (out_replaced) {
        *out_replaced = nullptr;
    }
    return false;
}

inline void* clone_pnext_chain(const void* next) {
    return vku::SafePnextCopy(next);
}

inline void free_cloned_pnext_chain(const void* next) {
    vku::FreePnextChain(next);
}

template <typename Predicate>
uint32_t filter_cloned_pnext_chain(
    const void** io_head,
    Predicate&& should_keep) {
    if (!io_head) {
        return 0;
    }

    uint32_t removed_count = 0;
    auto** previous_next = const_cast<VkBaseOutStructure**>(
        reinterpret_cast<const VkBaseOutStructure* const*>(io_head));
    auto* current = *previous_next;
    while (current) {
        auto* next = current->pNext;
        if (!should_keep(*current)) {
            *previous_next = next;
            current->pNext = nullptr;
            vku::FreePnextChain(current);
            ++removed_count;
            current = next;
            continue;
        }
        previous_next = &current->pNext;
        current = next;
    }
    return removed_count;
}

template <size_t N>
uint32_t filter_cloned_pnext_chain_by_stype(
    const void** io_head,
    const std::array<VkStructureType, N>& allowed_types) {
    return filter_cloned_pnext_chain(
        io_head,
        [&](const VkBaseOutStructure& current) {
            for (VkStructureType allowed : allowed_types) {
                if (current.sType == allowed) {
                    return true;
                }
            }
            return false;
        });
}
