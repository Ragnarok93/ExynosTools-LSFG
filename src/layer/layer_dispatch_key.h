#pragma once

template <typename T>
inline void* dispatch_key(T handle) {
    return *reinterpret_cast<void**>(handle);
}
