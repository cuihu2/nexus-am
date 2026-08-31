#ifndef HPU_DMA_H
#define HPU_DMA_H

#include <hpu/encoding.h>

/* custom1：x10 是相对 line 偏移，x11 是 line 数量。 */
#define DMA_INSN(word_, line_, count_)                                      \
    do {                                                                    \
        register uintptr_t hpu_line_reg __asm__("x10") = (line_);          \
        register uintptr_t hpu_count_reg __asm__("x11") = (count_);        \
        __asm__ volatile(".word %2"                                        \
                         :                                                  \
                         : "r"(hpu_line_reg), "r"(hpu_count_reg),          \
                           "i"((uint32_t)(word_))                           \
                         : "memory");                                      \
    } while (0)

enum {
    P0 = 0,
    P1 = 1,
    P2 = 2,
    P3 = 3,
    P4 = 4,
    P5 = 5,
    P6 = 6,
    P7 = 7
};

/* 对象号是参数，不再把 p0/p1/p2 写进函数名。 */
static inline int dload(unsigned object, uintptr_t line, uintptr_t count) {
    switch (object) {
    case P0:
        DMA_INSN(HPU_INSN_DLOAD_P0_POLY, line, count);
        return 0;
    case P1:
        DMA_INSN(HPU_INSN_DLOAD_P1_POLY, line, count);
        return 0;
    default:
        return 1;
    }
}

static inline int dload_mod(unsigned object, uintptr_t line,
                            uintptr_t count) {
    if (object != P4) return 1;
    DMA_INSN(HPU_INSN_DLOAD_P4_MOD, line, count);
    return 0;
}

static inline int dstore(unsigned object, uintptr_t line, uintptr_t count) {
    switch (object) {
    case P0:
        DMA_INSN(HPU_INSN_DSTORE_P0_RELEASE, line, count);
        return 0;
    case P2:
        DMA_INSN(HPU_INSN_DSTORE_P2_RELEASE, line, count);
        return 0;
    default:
        return 1;
    }
}

/* 兼容旧冒烟源码；新源码只使用上面的通用接口。 */
#define HPU_DMA_INSN DMA_INSN

#endif
