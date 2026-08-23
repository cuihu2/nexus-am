#ifndef HPU_LAYOUT_H
#define HPU_LAYOUT_H

#include <stdint.h>

#define HPU_MEM_BASE          UINT64_C(0x87000000)
#define HPU_WINDOW_LINES      64U
#define HPU_LINE_BYTES        256U
#define HPU_WORDS_PER_LINE    64U
#define HPU_CACHE_BLOCK_BYTES 64U
#define HPU_TIMEOUT           20000000U

#define HPU_LINE_MOD          0U
#define HPU_LINE_SRC_A        4U
#define HPU_LINE_SRC_B        5U
#define HPU_LINE_OUTPUT       8U
#define HPU_MODULUS           65537U

static inline void hpu_fence(void) {
    __asm__ volatile("fence rw, rw" : : : "memory");
}

static inline volatile uint32_t *hpu_line(unsigned line) {
    return (volatile uint32_t *)(uintptr_t)
        (HPU_MEM_BASE + (uintptr_t)line * HPU_LINE_BYTES);
}

#endif
