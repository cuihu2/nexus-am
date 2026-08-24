#ifndef HPU_SYNC_H
#define HPU_SYNC_H

#include <hpu/encoding.h>

static inline void hpu_psync(void) {
    __asm__ volatile(".word %0"
                     : : "i"(HPU_INSN_PSYNC) : "memory");
}

#endif
