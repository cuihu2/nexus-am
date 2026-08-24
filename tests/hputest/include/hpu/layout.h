#ifndef HPU_LAYOUT_H
#define HPU_LAYOUT_H

#include <stdint.h>

#define HPU_MEM_BASE          UINT64_C(0x87000000)
#define HPU_WINDOW_LINES      256U
#define HPU_LINE_BYTES        256U
#define HPU_WORDS_PER_LINE    64U
#define HPU_CACHE_BLOCK_BYTES 64U
#define HPU_TIMEOUT           20000000U

#define HPU_RNS_COEFFICIENTS  4096U
#define HPU_RNS_LINES         (HPU_RNS_COEFFICIENTS / HPU_WORDS_PER_LINE)
#define HPU_RNS_BYTES         (HPU_RNS_COEFFICIENTS * sizeof(uint32_t))

#define HPU_LINE_MOD          0U
#define HPU_LINE_SRC_A        64U
#define HPU_LINE_SRC_B        128U
#define HPU_LINE_OUTPUT       192U
#define HPU_MODULUS           65537U

_Static_assert(HPU_RNS_COEFFICIENTS % HPU_WORDS_PER_LINE == 0U,
               "RNS polynomial must contain complete HPU lines");
_Static_assert(HPU_LINE_OUTPUT + HPU_RNS_LINES <= HPU_WINDOW_LINES,
               "RNS input/output layout exceeds the HPU window");

static inline void hpu_fence(void) {
    __asm__ volatile("fence rw, rw" : : : "memory");
}

static inline volatile uint32_t *hpu_line(unsigned line) {
    return (volatile uint32_t *)(uintptr_t)
        (HPU_MEM_BASE + (uintptr_t)line * HPU_LINE_BYTES);
}

#endif
