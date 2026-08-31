#ifndef HPU_LAYOUT_H
#define HPU_LAYOUT_H

#include <hpu/inline_asm_mm_delivery.h>
#include <stdint.h>

#define MEM_BASE          UINT64_C(0x87000000)
#define SMOKE_LINES       256U
#define LINE_BYTES        256U
#define WORDS_PER_LINE    64U
#define CACHE_BLOCK_BYTES 64U
#define TIMEOUT           20000000U

#define RNS_COEFFICIENTS HPU_MM_COEFFICIENTS
#define RNS_LINES        (RNS_COEFFICIENTS / WORDS_PER_LINE)
#define RNS_BYTES        (RNS_COEFFICIENTS * sizeof(uint32_t))

/*
 * Relative line layout from inline-asm outputs/mm/test_data/hardware/line_map.csv.
 * The absolute HPU_MEM base remains a Nexus-AM platform choice.
 */
#define LINE_A  HPU_MM_LINE_SRC_A
#define LINE_B  HPU_MM_LINE_SRC_B
#define LINE_OUT HPU_MM_LINE_OUTPUT
#define LINE_MOD HPU_MM_LINE_MOD
#define MODULUS  HPU_MM_MODULUS

/* 旧名称只为已经迁入的用例保留；新冒烟用例统一使用上面的短名称。 */
#define HPU_MEM_BASE          MEM_BASE
#define HPU_WINDOW_LINES      SMOKE_LINES
#define HPU_LINE_BYTES        LINE_BYTES
#define HPU_WORDS_PER_LINE    WORDS_PER_LINE
#define HPU_CACHE_BLOCK_BYTES CACHE_BLOCK_BYTES
#define HPU_TIMEOUT           TIMEOUT
#define HPU_RNS_COEFFICIENTS  RNS_COEFFICIENTS
#define HPU_RNS_LINES         RNS_LINES
#define HPU_RNS_BYTES         RNS_BYTES
#define HPU_LINE_SRC_A        LINE_A
#define HPU_LINE_SRC_B        LINE_B
#define HPU_LINE_OUTPUT       LINE_OUT
#define HPU_LINE_MOD          LINE_MOD
#define HPU_MODULUS           MODULUS

_Static_assert(RNS_COEFFICIENTS == 4096U,
               "HPU smoke 001 requires the N=4096 MM fixture");
_Static_assert(MODULUS == UINT32_C(50061313),
               "HPU smoke 001 requires inline-asm MM basis-0 modulus");
_Static_assert(RNS_COEFFICIENTS % WORDS_PER_LINE == 0U,
               "RNS polynomial must contain complete HPU lines");
_Static_assert(HPU_MM_LINES_SRC_A == RNS_LINES,
               "inline-asm MM input A line count changed");
_Static_assert(HPU_MM_LINES_SRC_B == RNS_LINES,
               "inline-asm MM input B line count changed");
_Static_assert(HPU_MM_LINES_OUTPUT == RNS_LINES,
               "inline-asm MM output line count changed");
_Static_assert(HPU_MM_LINES_MOD == 1U,
               "inline-asm MM modulus context must occupy one line");
_Static_assert(LINE_A + RNS_LINES == LINE_B,
               "inline-asm MM input A/B layout changed");
_Static_assert(LINE_B + RNS_LINES == LINE_OUT,
               "inline-asm MM input/output layout changed");
_Static_assert(LINE_OUT + RNS_LINES == LINE_MOD,
               "inline-asm MM output/modulus layout changed");
_Static_assert(LINE_MOD + 1U <= SMOKE_LINES,
               "inline-asm MM fixture exceeds the HPU window");

static inline void mem_fence(void) {
    __asm__ volatile("fence rw, rw" : : : "memory");
}

static inline volatile uint32_t *line_ptr(unsigned line) {
    return (volatile uint32_t *)(uintptr_t)
        (MEM_BASE + (uintptr_t)line * LINE_BYTES);
}

static inline void hpu_fence(void) {
    mem_fence();
}

static inline volatile uint32_t *hpu_line(unsigned line) {
    return line_ptr(line);
}

#endif
