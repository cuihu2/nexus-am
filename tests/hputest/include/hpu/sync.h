#ifndef HPU_SYNC_H
#define HPU_SYNC_H

#include <hpu/csr.h>
#include <hpu/layout.h>
#include <klib.h>
#include <stdint.h>

static inline void hpu_psync(void) {
    __asm__ volatile(".word %0"
                     : : "i"((uint32_t)0x7000000bU) : "memory");
}

enum {
    HPU_COMPLETION_FAULT_ERROR = 1,
    HPU_COMPLETION_TIMEOUT_ERROR = 2,
    HPU_COMPLETION_CLEAR_ERROR = 3,
    HPU_COMPLETION_BUSY_ERROR = 4,
};

static inline int hpu_wait_completion_and_clear(void) {
    uint32_t status = 0U;
    unsigned timeout;

    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) {
            return HPU_COMPLETION_FAULT_ERROR;
        }
        if ((hpu_csr_read(HPU_CSR_IRQ) & 1U) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return HPU_COMPLETION_TIMEOUT_ERROR;
    if ((status & HPU_STATUS_BUSY) != 0U) return HPU_COMPLETION_BUSY_ERROR;

    printf("HPU_SMOKE_COMPLETION polls=%u status=0x%x irq=1\n",
           timeout + 1U, status);
    hpu_csr_write(HPU_CSR_IRQ, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT / 16U; ++timeout) {
        if ((hpu_csr_read(HPU_CSR_IRQ) & 1U) == 0U) break;
    }
    hpu_csr_write(HPU_CSR_IRQ, 0U);
    if (timeout == HPU_TIMEOUT / 16U) {
        return HPU_COMPLETION_CLEAR_ERROR;
    }
    return 0;
}

#endif
