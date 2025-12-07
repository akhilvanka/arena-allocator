#pragma once
/// Deterministic memory allocators for real-time and embedded systems.
///
/// In flight software, dynamic allocation with malloc/free is forbidden during
/// mission-critical phases because:
///   1. Non-deterministic latency (OS may call sbrk, trigger OOM handling)
///   2. Heap fragmentation degrades over time
///   3. malloc has no worst-case guarantee
///
/// These allocators solve the problem by pre-allocating a fixed backing store:
///
///   BumpAllocator   — O(1) alloc, bulk-free only (arena reset). Zero overhead.
///                     Ideal for per-frame or per-request scratch memory.
///
///   PoolAllocator   — O(1) alloc AND free for fixed-size objects. Freelist
///                     embedded directly in the free blocks — zero overhead.
///                     Ideal for messages, tasks, packets.
///
///   StackAllocator  — LIFO alloc/free with rewind markers. Enables nested
///                     scopes that each get a contiguous scratch region.
///
/// All allocators are non-copyable and operate on caller-provided backing
/// storage so they can live entirely on the stack or in static memory.
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <new>
#include <memory>
#include <type_traits>
#include <cstring>
#include <algorithm>

namespace alloc {

static constexpr size_t DEFAULT_ALIGN = alignof(std::max_align_t);

constexpr size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

// ---- BumpAllocator ----

class BumpAllocator {
public:
    BumpAllocator(void* buf, size_t capacity) noexcept
        : base_(static_cast<uint8_t*>(buf)),
          capacity_(capacity),
          offset_(0) {}

    void* allocate(size_t size, size_t align = DEFAULT_ALIGN) noexcept {
        size_t aligned = align_up(offset_, align);
        if (aligned + size > capacity_) return nullptr;
        offset_ = aligned + size;
        return base_ + aligned;
    }

    template<typename T, typename... Args>
    T* make(Args&&... args) {
        void* p = allocate(sizeof(T), alignof(T));
        if (!p) return nullptr;
        return ::new(p) T{std::forward<Args>(args)...};
    }

    void reset() noexcept { offset_ = 0; }

    size_t used()      const noexcept { return offset_; }
    size_t remaining() const noexcept { return capacity_ - offset_; }
    size_t capacity()  const noexcept { return capacity_; }

    BumpAllocator(const BumpAllocator&)            = delete;
    BumpAllocator& operator=(const BumpAllocator&) = delete;

private:
    uint8_t* base_;
    size_t   capacity_;
    size_t   offset_;
};

// ---- PoolAllocator ----
// Fixed-size object pool — freelist stored inside free blocks (no overhead).
// Object size must be >= sizeof(void*).
} // namespace alloc
