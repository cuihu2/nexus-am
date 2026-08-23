#ifndef HPU_CSR_H
#define HPU_CSR_H

#include <hpu/layout.h>

#define HPU_CSR_BASE    UINT64_C(0x08000000)
#define HPU_CSR_BASE_LO 0x00U
#define HPU_CSR_BASE_HI 0x04U
#define HPU_CSR_SIZE_LO 0x08U
#define HPU_CSR_SIZE_HI 0x0cU
#define HPU_CSR_COMMIT  0x10U
#define HPU_CSR_STATUS  0x14U
#define HPU_CSR_FAULT   0x18U
#define HPU_CSR_IRQ     0x1cU

#define HPU_STATUS_WINDOW_VALID (1U << 0)
#define HPU_STATUS_BUSY         (1U << 1)
#define HPU_STATUS_FAULT_VALID  (1U << 2)

static inline volatile uint32_t *hpu_csr_ptr(unsigned offset) {
    return (volatile uint32_t *)(uintptr_t)(HPU_CSR_BASE + offset);
}

static inline void hpu_csr_write(unsigned offset, uint32_t value) {
    *hpu_csr_ptr(offset) = value;
    hpu_fence();
}

static inline uint32_t hpu_csr_read(unsigned offset) {
    uint32_t value = *hpu_csr_ptr(offset);
    hpu_fence();
    return value;
}

#endif
