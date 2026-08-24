#ifndef HPU_INIT_H
#define HPU_INIT_H

#include <hpu/csr.h>
#include <hpu/layout.h>
#include <klib.h>

enum {
    HPU_INIT_CSR_READBACK_ERROR = 1,
    HPU_INIT_FAULT_ERROR = 2,
    HPU_INIT_TIMEOUT_ERROR = 3,
    HPU_INIT_NOT_IDLE_ERROR = 4,
};

static inline int hpu_initialize_and_verify(void) {
    uint32_t base_lo;
    uint32_t base_hi;
    uint32_t size_lo;
    uint32_t size_hi;
    uint32_t status = 0U;
    uint32_t fault;
    uint32_t irq;
    unsigned timeout;

    /* Clear stale events before programming the shadow configuration. */
    hpu_csr_write(HPU_CSR_FAULT, 1U);
    hpu_csr_write(HPU_CSR_IRQ, 1U);
    hpu_csr_write(HPU_CSR_IRQ, 0U);

    hpu_csr_write(HPU_CSR_BASE_LO, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write(HPU_CSR_BASE_HI, 0U);
    hpu_csr_write(HPU_CSR_SIZE_LO, HPU_WINDOW_LINES);
    hpu_csr_write(HPU_CSR_SIZE_HI, 0U);

    /* Readback proves that the CPU writes reached all four shadow CSRs. */
    base_lo = hpu_csr_read(HPU_CSR_BASE_LO);
    base_hi = hpu_csr_read(HPU_CSR_BASE_HI);
    size_lo = hpu_csr_read(HPU_CSR_SIZE_LO);
    size_hi = hpu_csr_read(HPU_CSR_SIZE_HI);
    printf("HPU_SMOKE_CSR_WRITEBACK base_lo=0x%x base_hi=0x%x "
           "size_lo=%u size_hi=0x%x\n",
           base_lo, base_hi, size_lo, size_hi);
    if (base_lo != (uint32_t)HPU_MEM_BASE || base_hi != 0U ||
        size_lo != HPU_WINDOW_LINES || size_hi != 0U) {
        return HPU_INIT_CSR_READBACK_ERROR;
    }

    /* window_valid after COMMIT proves that HPU accepted the configuration. */
    hpu_csr_write(HPU_CSR_COMMIT, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) {
            return HPU_INIT_FAULT_ERROR;
        }
        if ((status & HPU_STATUS_WINDOW_VALID) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return HPU_INIT_TIMEOUT_ERROR;

    fault = hpu_csr_read(HPU_CSR_FAULT);
    irq = hpu_csr_read(HPU_CSR_IRQ);
    printf("HPU_SMOKE_INIT status=0x%x fault=0x%x irq=0x%x "
           "window_valid=1\n",
           status, fault, irq);
    if ((status & HPU_STATUS_BUSY) != 0U ||
        (status & HPU_STATUS_FAULT_VALID) != 0U ||
        (fault & 1U) != 0U || (irq & 1U) != 0U) {
        return HPU_INIT_NOT_IDLE_ERROR;
    }
    return 0;
}

#endif
