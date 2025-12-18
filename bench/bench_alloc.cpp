/// Benchmark: BumpAllocator / PoolAllocator vs malloc/free.
/// Shows the deterministic O(1) latency advantage and throughput gain.
#include "arena.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <numeric>

using Clock = std::chrono::steady_clock;
using ns    = std::chrono::nanoseconds;

static double elapsed_ns(Clock::time_point t0) {
    return static_cast<double>(
        std::chrono::duration_cast<ns>(Clock::now() - t0).count());
}

struct Result { std::string label; double ns_per_op; };

// ---- Benchmark helpers ----

Result bench_malloc_free(size_t iters, size_t obj_size) {
    auto t0 = Clock::now();
    for (size_t i = 0; i < iters; ++i) {
        void* p = std::malloc(obj_size);
        // Touch the allocation to prevent optimizer elision
        static_cast<char*>(p)[0] = static_cast<char>(i);
        std::free(p);
    }
    return {"malloc/free (" + std::to_string(obj_size) + "B)",
            elapsed_ns(t0) / static_cast<double>(iters)};
}

Result bench_bump_alloc_reset(size_t iters, size_t obj_size) {
    static alloc::StaticBumpAllocator<1 << 20> bump; // 1 MB backing

    auto t0 = Clock::now();
    for (size_t i = 0; i < iters; ++i) {
        if (bump.remaining() < obj_size) bump.reset();
        void* p = bump.allocate(obj_size);
        static_cast<char*>(p)[0] = static_cast<char>(i);
    }
    return {"bump alloc     (" + std::to_string(obj_size) + "B)",
            elapsed_ns(t0) / static_cast<double>(iters)};
}

Result bench_pool_alloc_free(size_t iters, size_t obj_size) {
    // Each call gets a fresh static buffer (separate array per obj_size via template)
    static std::array<uint8_t, 1 << 20> pool_storage{};
    alloc::PoolAllocator pool(pool_storage.data(), pool_storage.size(), obj_size);

    auto t0 = Clock::now();
    for (size_t i = 0; i < iters; ++i) {
        void* p = pool.allocate();
        static_cast<char*>(p)[0] = static_cast<char>(i);
        pool.deallocate(p);
    }
    return {"pool alloc/free(" + std::to_string(obj_size) + "B)",
            elapsed_ns(t0) / static_cast<double>(iters)};
}

// Measure latency variance (shows determinism advantage)
Result bench_malloc_latency_max(size_t iters, size_t obj_size) {
    double max_ns = 0;
    for (size_t i = 0; i < iters; ++i) {
        auto t0 = Clock::now();
        void* p = std::malloc(obj_size);
        static_cast<char*>(p)[0] = 1;
        std::free(p);
        double d = elapsed_ns(t0);
        if (d > max_ns) max_ns = d;
    }
    return {"malloc worst-case (" + std::to_string(obj_size) + "B)", max_ns};
}

Result bench_bump_latency_max(size_t iters, size_t obj_size) {
    static alloc::StaticBumpAllocator<1 << 22> bump;
    double max_ns = 0;
    for (size_t i = 0; i < iters; ++i) {
        auto t0 = Clock::now();
        if (bump.remaining() < obj_size) bump.reset();
        void* p = bump.allocate(obj_size);
        static_cast<char*>(p)[0] = 1;
        double d = elapsed_ns(t0);
        if (d > max_ns) max_ns = d;
    }
    return {"bump worst-case   (" + std::to_string(obj_size) + "B)", max_ns};
}

int main() {
    constexpr size_t ITERS = 1'000'000;

    std::printf("=== Arena Allocator Benchmark ===\n");
    std::printf("Iterations: %zu\n\n", ITERS);

    std::printf("--- Average throughput (ns/op) ---\n");
    std::printf("%-38s  %10s\n", "Allocator", "ns/op");
    std::printf("%s\n", std::string(52, '-').c_str());

    auto print = [](Result r) {
        std::printf("%-38s  %10.2f\n", r.label.c_str(), r.ns_per_op);
    };

    for (size_t sz : {16UL, 64UL, 256UL}) {
        print(bench_malloc_free(ITERS, sz));
        print(bench_bump_alloc_reset(ITERS, sz));
        print(bench_pool_alloc_free(ITERS, sz));
        std::printf("\n");
    }

    std::printf("--- Worst-case single allocation latency ---\n");
    std::printf("(Critical for RT systems — malloc can spike due to OS interaction)\n\n");
    constexpr size_t LAT_ITERS = 100'000;
    print(bench_malloc_latency_max(LAT_ITERS, 64));
    print(bench_bump_latency_max(LAT_ITERS, 64));

    return 0;
}
