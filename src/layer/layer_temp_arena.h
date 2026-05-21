#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>

template <size_t InlineBytes = 4096u>
class TempArena {
public:
    TempArena() = default;

    TempArena(const TempArena&) = delete;
    TempArena& operator=(const TempArena&) = delete;

    void reset() {
        offset_ = 0;
        fallback_.reset();
        fallback_size_ = 0;
    }

    template <typename T>
    T* allocate_array(size_t count) {
        static_assert(std::is_trivially_destructible<T>::value, "TempArena only supports trivial arrays");
        if (count == 0) {
            return nullptr;
        }

        const size_t bytes = sizeof(T) * count;
        const size_t alignment = alignof(T);
        size_t aligned_offset = align_up(offset_, alignment);
        if (aligned_offset + bytes <= inline_storage_.size()) {
            auto* ptr = reinterpret_cast<T*>(inline_storage_.data() + aligned_offset);
            offset_ = aligned_offset + bytes;
            return ptr;
        }

        if (!fallback_ || fallback_size_ < bytes + alignment) {
            fallback_size_ = bytes + alignment;
            fallback_ = std::make_unique<uint8_t[]>(fallback_size_);
        }
        uintptr_t base = reinterpret_cast<uintptr_t>(fallback_.get());
        uintptr_t aligned = align_up_ptr(base, alignment);
        return reinterpret_cast<T*>(aligned);
    }

private:
    static size_t align_up(size_t value, size_t alignment) {
        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    static uintptr_t align_up_ptr(uintptr_t value, size_t alignment) {
        return (value + static_cast<uintptr_t>(alignment) - 1u) &
               ~static_cast<uintptr_t>(alignment - 1u);
    }

    std::array<uint8_t, InlineBytes> inline_storage_{};
    size_t offset_ = 0;
    std::unique_ptr<uint8_t[]> fallback_;
    size_t fallback_size_ = 0;
};
