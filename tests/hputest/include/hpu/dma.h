#ifndef HPU_DMA_H
#define HPU_DMA_H

#include <hpu/encoding.h>

/* custom1: x10=line offset, x11=line count. */
#define HPU_DMA_INSN(word_, line_, count_)                                  \
    do {                                                                    \
        register uintptr_t hpu_line_reg __asm__("x10") = (line_);          \
        register uintptr_t hpu_count_reg __asm__("x11") = (count_);        \
        __asm__ volatile(".word %2"                                        \
                         :                                                  \
                         : "r"(hpu_line_reg), "r"(hpu_count_reg),          \
                           "i"((uint32_t)(word_))                           \
                         : "memory");                                      \
    } while (0)

static inline void hpu_dload_p0(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(HPU_INSN_DLOAD_P0_POLY, line, count);
}

static inline void hpu_dload_p1(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(HPU_INSN_DLOAD_P1_POLY, line, count);
}

static inline void hpu_dload_mod_p4(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(HPU_INSN_DLOAD_P4_MOD, line, count);
}

static inline void hpu_dstore_p0_release(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(HPU_INSN_DSTORE_P0_RELEASE, line, count);
}

static inline void hpu_dstore_p2_release(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(HPU_INSN_DSTORE_P2_RELEASE, line, count);
}

#endif
