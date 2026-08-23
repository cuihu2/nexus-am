#ifndef HPU_CACHE_H
#define HPU_CACHE_H

#include <hpu/layout.h>
#include <stddef.h>

static inline void hpu_cache_clean(uintptr_t address, size_t bytes) {
    uintptr_t cursor = address & ~((uintptr_t)HPU_CACHE_BLOCK_BYTES - 1U);
    uintptr_t end = address + bytes;

    for (; cursor < end; cursor += HPU_CACHE_BLOCK_BYTES) {
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 2"
                         : : "r"(cursor) : "memory");
    }
    hpu_fence();
}

static inline void hpu_cache_invalidate(uintptr_t address, size_t bytes) {
    uintptr_t cursor = address & ~((uintptr_t)HPU_CACHE_BLOCK_BYTES - 1U);
    uintptr_t end = address + bytes;

    for (; cursor < end; cursor += HPU_CACHE_BLOCK_BYTES) {
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 0"
                         : : "r"(cursor) : "memory");
    }
    hpu_fence();
}

#endif
