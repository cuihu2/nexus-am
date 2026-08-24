#ifndef HPU_ARITHMETIC_H
#define HPU_ARITHMETIC_H

#include <hpu/encoding.h>

#define HPU_CUSTOM0_INSN(word_)                                             \
    __asm__ volatile(".word %0" : : "i"((uint32_t)(word_)) : "memory")

static inline void hpu_pmodld_0(void) {
    HPU_CUSTOM0_INSN(HPU_INSN_PMODLD_0);
}

static inline void hpu_padd_p2_p0_p1(void) {
    HPU_CUSTOM0_INSN(HPU_INSN_PADD_P2_P0_P1);
}

#endif
