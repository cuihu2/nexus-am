#ifndef HPU_DMA_H
#define HPU_DMA_H

#include <stdint.h>

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
    HPU_DMA_INSN(0x00b5102bU, line, count);
}

static inline void hpu_dload_p1(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(0x00b5122bU, line, count);
}

static inline void hpu_dload_mod_p4(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(0x00b5292bU, line, count);
}

static inline void hpu_dstore_p0_release(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(0x00b5502bU, line, count);
}

static inline void hpu_dstore_p2_release(uintptr_t line, uintptr_t count) {
    HPU_DMA_INSN(0x00b5542bU, line, count);
}

#endif
