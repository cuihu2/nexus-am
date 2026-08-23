#ifndef HPU_ARITHMETIC_H
#define HPU_ARITHMETIC_H

#include <stdint.h>

#define HPU_CUSTOM0_INSN(word_)                                             \
    __asm__ volatile(".word %0" : : "i"((uint32_t)(word_)) : "memory")

static inline void hpu_pmodld_0(void) {
    HPU_CUSTOM0_INSN(0x6000000bU);
}

static inline void hpu_padd_p2_p0_p1(void) {
    HPU_CUSTOM0_INSN(0x0400400bU);
}

#endif
