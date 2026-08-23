#ifndef HPU_SYNC_H
#define HPU_SYNC_H

#include <stdint.h>

static inline void hpu_psync(void) {
    __asm__ volatile(".word %0"
                     : : "i"((uint32_t)0x7000000bU) : "memory");
}

#endif
