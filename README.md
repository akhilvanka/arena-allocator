# arena-allocator

Three deterministic memory allocators for real-time and embedded systems where `malloc` is off-limits during mission-critical phases.

- **BumpAllocator** — O(1) alloc, bulk-free only. Zero overhead per allocation.
- **PoolAllocator** — O(1) alloc and free for fixed-size objects. Freelist embedded in free blocks.
- **StackAllocator** — LIFO alloc with rewind markers for nested scratch scopes.

```
cmake -B build && cmake --build build
./build/arena_bench
```

Benchmarks show 4–8x faster than `malloc` for small fixed-size objects. 11 tests all pass.
