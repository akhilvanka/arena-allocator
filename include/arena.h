#pragma once
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

class PoolAllocator {
public:
    PoolAllocator(void* buf, size_t buf_size, size_t object_size,
                  size_t align = DEFAULT_ALIGN) noexcept
        : object_size_(align_up(std::max(object_size, sizeof(void*)), align)),
          capacity_(buf_size / object_size_),
          free_count_(capacity_)
    {
        // Build freelist
        head_ = nullptr;
        uint8_t* p = static_cast<uint8_t*>(buf);
        for (size_t i = capacity_; i-- > 0;) {
            void** slot = reinterpret_cast<void**>(p + i * object_size_);
            *slot = head_;
            head_ = slot;
        }
    }

    void* allocate() noexcept {
        if (!head_) return nullptr;
        void* p = head_;
        head_   = *static_cast<void**>(head_);
        --free_count_;
        return p;
    }

    void deallocate(void* p) noexcept {
        if (!p) return;
        *static_cast<void**>(p) = head_;
        head_ = p;
        ++free_count_;
    }

    template<typename T, typename... Args>
    T* construct(Args&&... args) {
        void* p = allocate();
        if (!p) return nullptr;
        return new(p) T(std::forward<Args>(args)...);
    }

    template<typename T>
    void destroy(T* p) noexcept {
        if (!p) return;
        p->~T();
        deallocate(p);
    }

    size_t free_count()  const noexcept { return free_count_; }
    size_t capacity()    const noexcept { return capacity_; }
    size_t used()        const noexcept { return capacity_ - free_count_; }
    size_t object_size() const noexcept { return object_size_; }

    PoolAllocator(const PoolAllocator&)            = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

private:
    size_t  object_size_;
    size_t  capacity_;
    size_t  free_count_;
    void*   head_{nullptr};
};

// ---- StackAllocator with rewind markers ----