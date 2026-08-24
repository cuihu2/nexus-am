#ifndef HPU_LAYOUT_H
#define HPU_LAYOUT_H

#include <hpu/inline_asm_mm_delivery.h>
#include <stdint.h>

#define HPU_MEM_BASE          UINT64_C(0x87000000)
#define HPU_WINDOW_LINES      256U
#define HPU_LINE_BYTES        256U
#define HPU_WORDS_PER_LINE    64U
#define HPU_CACHE_BLOCK_BYTES 64U
#define HPU_TIMEOUT           20000000U

#define HPU_RNS_COEFFICIENTS  HPU_MM_COEFFICIENTS
#define HPU_RNS_LINES         (HPU_RNS_COEFFICIENTS / HPU_WORDS_PER_LINE)
#define HPU_RNS_BYTES         (HPU_RNS_COEFFICIENTS * sizeof(uint32_t))

/*
 * Relative line layout from inline-asm outputs/mm/test_data/hardware/line_map.csv.
 * The absolute HPU_MEM base remains a Nexus-AM platform choice.
 */
#define HPU_LINE_SRC_A        HPU_MM_LINE_SRC_A
#define HPU_LINE_SRC_B        HPU_MM_LINE_SRC_B
#define HPU_LINE_OUTPUT       HPU_MM_LINE_OUTPUT
#define HPU_LINE_MOD          HPU_MM_LINE_MOD
#define HPU_MODULUS           HPU_MM_MODULUS

_Static_assert(HPU_RNS_COEFFICIENTS == 4096U,
               "HPU smoke 001 requires the N=4096 MM fixture");
_Static_assert(HPU_MODULUS == UINT32_C(50061313),
               "HPU smoke 001 requires inline-asm MM basis-0 modulus");
_Static_assert(HPU_RNS_COEFFICIENTS % HPU_WORDS_PER_LINE == 0U,
               "RNS polynomial must contain complete HPU lines");
_Static_assert(HPU_MM_LINES_SRC_A == HPU_RNS_LINES,
               "inline-asm MM input A line count changed");
_Static_assert(HPU_MM_LINES_SRC_B == HPU_RNS_LINES,
               "inline-asm MM input B line count changed");
_Static_assert(HPU_MM_LINES_OUTPUT == HPU_RNS_LINES,
               "inline-asm MM output line count changed");
_Static_assert(HPU_MM_LINES_MOD == 1U,
               "inline-asm MM modulus context must occupy one line");
_Static_assert(HPU_LINE_SRC_A + HPU_RNS_LINES == HPU_LINE_SRC_B,
               "inline-asm MM input A/B layout changed");
_Static_assert(HPU_LINE_SRC_B + HPU_RNS_LINES == HPU_LINE_OUTPUT,
               "inline-asm MM input/output layout changed");
_Static_assert(HPU_LINE_OUTPUT + HPU_RNS_LINES == HPU_LINE_MOD,
               "inline-asm MM output/modulus layout changed");
_Static_assert(HPU_LINE_MOD + 1U <= HPU_WINDOW_LINES,
               "inline-asm MM fixture exceeds the HPU window");

static inline void hpu_fence(void) {
    __asm__ volatile("fence rw, rw" : : : "memory");
}

static inline volatile uint32_t *hpu_line(unsigned line) {
    return (volatile uint32_t *)(uintptr_t)
        (HPU_MEM_BASE + (uintptr_t)line * HPU_LINE_BYTES);
}

#endif
