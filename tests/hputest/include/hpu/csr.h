#ifndef HPU_CSR_H
#define HPU_CSR_H

#include <hpu/layout.h>

#define HPU_CSR_MMIO_BASE    UINT64_C(0x08000000)
#define HPU_CSR_BASE_LO_ADDR (HPU_CSR_MMIO_BASE + UINT64_C(0x00))
#define HPU_CSR_BASE_HI_ADDR (HPU_CSR_MMIO_BASE + UINT64_C(0x04))
#define HPU_CSR_SIZE_LO_ADDR (HPU_CSR_MMIO_BASE + UINT64_C(0x08))
#define HPU_CSR_SIZE_HI_ADDR (HPU_CSR_MMIO_BASE + UINT64_C(0x0c))
#define HPU_CSR_COMMIT_ADDR  (HPU_CSR_MMIO_BASE + UINT64_C(0x10))
#define HPU_CSR_STATUS_ADDR  (HPU_CSR_MMIO_BASE + UINT64_C(0x14))
#define HPU_CSR_FAULT_ADDR   (HPU_CSR_MMIO_BASE + UINT64_C(0x18))
#define HPU_CSR_IRQ_ADDR     (HPU_CSR_MMIO_BASE + UINT64_C(0x1c))

#define HPU_STATUS_WINDOW_VALID (1U << 0)
#define HPU_STATUS_BUSY         (1U << 1)
#define HPU_STATUS_FAULT_VALID  (1U << 2)
#define HPU_FAULT_VALID         (1U << 0)
#define HPU_IRQ_LEVEL           (1U << 0)
#define HPU_COMMIT_REQUEST      (1U << 0)

/*
 * These are intentionally address-granularity MMIO operations.  A testcase
 * passes an explicit HPU_CSR_*_ADDR macro so its CSR access remains visible
 * in main() and in the generated disassembly.
 */
static inline void hpu_csr_write32(uintptr_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
    hpu_fence();
}

static inline uint32_t hpu_csr_read32(uintptr_t address) {
    uint32_t value = *(volatile uint32_t *)address;
    hpu_fence();
    return value;
}

#endif
