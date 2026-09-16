#pragma once

#include <cstddef>

#include "yyjson.h"

#include "platform/windows_headers.hpp"

namespace squarestar::json_memory {

// yyjson documents are short-lived and can allocate many small nodes while a
// provider response is being parsed. On Windows, isolate those allocations in
// a document-private heap. Destroying the heap after the document is consumed
// returns its committed pages to the OS instead of leaving them available in
// the long-lived CRT/process heap.
//
// Each allocator instance belongs to exactly one parse on one thread, so the
// Windows private heap can safely skip heap serialization.
struct AllocatorOwner {
    void* handle = nullptr;
};

#ifdef _WIN32

inline void* HeapMalloc(void* context, std::size_t size) noexcept {
    if (!context || size == 0)
        return nullptr;
    return HeapAlloc(static_cast<HANDLE>(context), 0, size);
}

inline void* HeapRealloc(void* context,
                         void* memory,
                         std::size_t,
                         std::size_t size) noexcept {
    if (!context)
        return nullptr;
    if (!memory)
        return size == 0 ? nullptr : HeapAlloc(static_cast<HANDLE>(context), 0, size);
    if (size == 0) {
        HeapFree(static_cast<HANDLE>(context), 0, memory);
        return nullptr;
    }
    return HeapReAlloc(static_cast<HANDLE>(context), 0, memory, size);
}

inline void HeapFreeBlock(void* context, void* memory) noexcept {
    if (context && memory)
        HeapFree(static_cast<HANDLE>(context), 0, memory);
}

inline const yyjson_alc* Initialize(yyjson_alc& allocator,
                                    AllocatorOwner& owner) noexcept {
    HANDLE heap = HeapCreate(HEAP_NO_SERIALIZE, 0, 0);
    if (!heap)
        return nullptr;
    owner.handle = heap;
    allocator.malloc = HeapMalloc;
    allocator.realloc = HeapRealloc;
    allocator.free = HeapFreeBlock;
    allocator.ctx = heap;
    return &allocator;
}

inline void Release(AllocatorOwner& owner) noexcept {
    if (!owner.handle)
        return;
    HeapDestroy(static_cast<HANDLE>(owner.handle));
    owner.handle = nullptr;
}

#else

inline const yyjson_alc* Initialize(yyjson_alc&,
                                    AllocatorOwner&) noexcept {
    // Portable tests keep yyjson's default allocator. The production Windows
    // build is where Private Bytes/heap retention is relevant.
    return nullptr;
}

inline void Release(AllocatorOwner&) noexcept {}

#endif

} // namespace squarestar::json_memory
