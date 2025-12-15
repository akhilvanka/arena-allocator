#include "arena.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cassert>
#include <new>

using namespace alloc;

#define PASS(n) std::printf("PASS: %s\n", n)
#define FAIL(n,m) do { std::fprintf(stderr, "FAIL: %s — %s\n", n, m); std::exit(1); } while(0)

// ---- BumpAllocator ----

void test_bump_basic() {
    alignas(64) uint8_t buf[256];
    BumpAllocator a(buf, sizeof(buf));

    if (a.used() != 0)      FAIL("bump_basic", "should start at 0");
    if (a.capacity() != 256) FAIL("bump_basic", "wrong capacity");

    void* p1 = a.allocate(16);
    void* p2 = a.allocate(32);
    if (!p1 || !p2) FAIL("bump_basic", "allocation failed");
    if (p2 <= p1)   FAIL("bump_basic", "p2 should be after p1");
    if (a.used() != 48) FAIL("bump_basic", "wrong used count");
    PASS("bump_basic");
}

void test_bump_alignment() {
    alignas(64) uint8_t buf[512];
    BumpAllocator a(buf, sizeof(buf));

    a.allocate(1);             // leaves offset at 1
    void* p = a.allocate(4, 4); // next 4-aligned addr
    if (reinterpret_cast<uintptr_t>(p) % 4 != 0)
        FAIL("bump_alignment", "not 4-aligned");

    a.allocate(1);
    void* q = a.allocate(8, 8);
    if (reinterpret_cast<uintptr_t>(q) % 8 != 0)
        FAIL("bump_alignment", "not 8-aligned");
    PASS("bump_alignment");
}

void test_bump_overflow() {
    alignas(16) uint8_t buf[32];
    BumpAllocator a(buf, sizeof(buf));
    void* p = a.allocate(32);
    if (!p) FAIL("bump_overflow", "should fit exactly");
    void* q = a.allocate(1);
    if (q)  FAIL("bump_overflow", "should fail when full");
    PASS("bump_overflow");
}

void test_bump_reset() {
    alignas(16) uint8_t buf[128];
    BumpAllocator a(buf, sizeof(buf));
    a.allocate(64);
    if (a.used() != 64) FAIL("bump_reset", "wrong used before reset");
    a.reset();
    if (a.used() != 0)  FAIL("bump_reset", "reset didn't clear offset");
    void* p = a.allocate(128);
    if (!p) FAIL("bump_reset", "full alloc after reset failed");
    PASS("bump_reset");
}

void test_bump_make() {
    alignas(16) uint8_t buf[256];
    BumpAllocator a(buf, sizeof(buf));

    struct Point { int x, y; Point(int a, int b) : x(a), y(b) {} };
    auto* p = a.make<Point>(3, 7);
    if (!p || p->x != 3 || p->y != 7)
        FAIL("bump_make", "constructed object wrong");
    PASS("bump_make");
}

// ---- PoolAllocator ----

void test_pool_basic() {
    alignas(16) uint8_t buf[64 * 16];
    PoolAllocator pool(buf, sizeof(buf), 16);

    if (pool.capacity() != 64) FAIL("pool_basic", "wrong capacity");

    void* p1 = pool.allocate();
    void* p2 = pool.allocate();
    if (!p1 || !p2)   FAIL("pool_basic", "alloc failed");
    if (p1 == p2)     FAIL("pool_basic", "returned same block");
    if (pool.used() != 2) FAIL("pool_basic", "wrong used count");

    pool.deallocate(p1);
    if (pool.used() != 1) FAIL("pool_basic", "wrong count after free");
    void* p3 = pool.allocate();
    if (p3 != p1) FAIL("pool_basic", "freelist should return last freed block");
    PASS("pool_basic");
}

void test_pool_exhaustion() {
    alignas(16) uint8_t buf[3 * 16];
    PoolAllocator pool(buf, sizeof(buf), 16);

    void *a = pool.allocate(), *b = pool.allocate(), *c = pool.allocate();
    if (!a || !b || !c) FAIL("pool_exhaustion", "alloc failed");
    void* d = pool.allocate();
    if (d) FAIL("pool_exhaustion", "should return null when exhausted");

    pool.deallocate(b);
    void* e = pool.allocate();
    if (!e) FAIL("pool_exhaustion", "alloc after free failed");
    PASS("pool_exhaustion");
}

struct Counter {
    static int live;
    int id;
    explicit Counter(int i) : id(i) { ++live; }
    ~Counter() { --live; }
};
int Counter::live = 0;

void test_pool_construct_destroy() {
    alignas(16) uint8_t buf[8 * 64];
    PoolAllocator pool(buf, sizeof(buf), 64);
    Counter::live = 0;

    auto* c1 = pool.construct<Counter>(1);
    auto* c2 = pool.construct<Counter>(2);
    if (Counter::live != 2) FAIL("pool_construct", "constructor not called");
    if (c1->id != 1 || c2->id != 2) FAIL("pool_construct", "wrong id");

    pool.destroy(c1);
    if (Counter::live != 1) FAIL("pool_construct", "destructor not called");
    PASS("pool_construct_destroy");
}

// ---- StackAllocator ----

void test_stack_marker_rewind() {
    alignas(16) uint8_t buf[256];
    StackAllocator a(buf, sizeof(buf));

    a.allocate(32);
    auto mark = a.get_marker();
    if (mark != 32) FAIL("stack_marker", "wrong marker value");

    a.allocate(64);
    if (a.used() != 96) FAIL("stack_marker", "wrong used after second alloc");

    a.rewind(mark);
    if (a.used() != 32) FAIL("stack_marker", "rewind failed");

    // Allocate again from the rewound position
    void* p = a.allocate(64);
    if (!p) FAIL("stack_marker", "alloc after rewind failed");
    if (a.used() != 96) FAIL("stack_marker", "wrong used after re-alloc");
    PASS("stack_marker_rewind");
}

void test_scoped_stack() {
    alignas(16) uint8_t buf[512];
    StackAllocator a(buf, sizeof(buf));

    a.allocate(64);  // persistent allocation
    if (a.used() != 64) FAIL("scoped_stack", "baseline wrong");

    {
        ScopedStack scope(a);
        scope.allocate(128);
        if (a.used() != 192) FAIL("scoped_stack", "scope alloc wrong");

        {
            ScopedStack inner(a);
            inner.allocate(64);
            if (a.used() != 256) FAIL("scoped_stack", "inner scope wrong");
        }
        // inner scope rewound
        if (a.used() != 192) FAIL("scoped_stack", "inner scope not rewound");
    }
    // outer scope rewound
    if (a.used() != 64) FAIL("scoped_stack", "outer scope not rewound");
    PASS("scoped_stack");
}

// ---- StaticAllocators ----

void test_static_allocators() {
    StaticBumpAllocator<256> bump;
    auto* p = bump.make<double>(3.14);
    if (!p || *p < 3.13) FAIL("static_allocators", "bump failed");

    StaticPoolAllocator<32, 16> pool;
    if (pool.capacity() != 16) FAIL("static_allocators", "wrong pool capacity");
    void* slot = pool.allocate();
    if (!slot) FAIL("static_allocators", "pool alloc failed");
    pool.deallocate(slot);
    if (pool.used() != 0) FAIL("static_allocators", "pool free failed");

    StaticStackAllocator<128> stack;
    auto m = stack.get_marker();
    stack.allocate(64);
    stack.rewind(m);
    if (stack.used() != 0) FAIL("static_allocators", "stack rewind failed");
    PASS("static_allocators");
}

int main() {
    std::printf("Running arena allocator tests...\n\n");
    test_bump_basic();
    test_bump_alignment();
    test_bump_overflow();
    test_bump_reset();
    test_bump_make();
    test_pool_basic();
    test_pool_exhaustion();
    test_pool_construct_destroy();
    test_stack_marker_rewind();
    test_scoped_stack();
    test_static_allocators();
    std::printf("\nAll tests passed.\n");
    return 0;
}
