#ifndef HPU_CSR_H
#define HPU_CSR_H

#include <hpu/layout.h>

#define CSR_BASE    UINT64_C(0x08000000)
#define CSR_BASE_LO (CSR_BASE + UINT64_C(0x00))
#define CSR_BASE_HI (CSR_BASE + UINT64_C(0x04))
#define CSR_SIZE_LO (CSR_BASE + UINT64_C(0x08))
#define CSR_SIZE_HI (CSR_BASE + UINT64_C(0x0c))
#define CSR_COMMIT  (CSR_BASE + UINT64_C(0x10))
#define CSR_STATUS  (CSR_BASE + UINT64_C(0x14))
#define CSR_FAULT   (CSR_BASE + UINT64_C(0x18))
#define CSR_IRQ     (CSR_BASE + UINT64_C(0x1c))

#define STATUS_VALID (1U << 0)
#define STATUS_BUSY  (1U << 1)
#define STATUS_FAULT (1U << 2)
#define FAULT_VALID  (1U << 0)
#define IRQ_LEVEL    (1U << 0)
#define COMMIT       (1U << 0)

/* 旧名称只为已迁入的 49 个用例保留。 */
#define HPU_CSR_MMIO_BASE       CSR_BASE
#define HPU_CSR_BASE_LO_ADDR    CSR_BASE_LO
#define HPU_CSR_BASE_HI_ADDR    CSR_BASE_HI
#define HPU_CSR_SIZE_LO_ADDR    CSR_SIZE_LO
#define HPU_CSR_SIZE_HI_ADDR    CSR_SIZE_HI
#define HPU_CSR_COMMIT_ADDR     CSR_COMMIT
#define HPU_CSR_STATUS_ADDR     CSR_STATUS
#define HPU_CSR_FAULT_ADDR      CSR_FAULT
#define HPU_CSR_IRQ_ADDR        CSR_IRQ
#define HPU_STATUS_WINDOW_VALID STATUS_VALID
#define HPU_STATUS_BUSY         STATUS_BUSY
#define HPU_STATUS_FAULT_VALID  STATUS_FAULT
#define HPU_FAULT_VALID         FAULT_VALID
#define HPU_IRQ_LEVEL           IRQ_LEVEL
#define HPU_COMMIT_REQUEST      COMMIT

/* 每次只访问一个显式 MMIO 地址，main() 和反汇编都能直接看到访问粒度。 */
static inline void csr_write(uintptr_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
    mem_fence();
}

static inline uint32_t csr_read(uintptr_t address) {
    uint32_t value = *(volatile uint32_t *)address;
    mem_fence();
    return value;
}

static inline void hpu_csr_write32(uintptr_t address, uint32_t value) {
    csr_write(address, value);
}

static inline uint32_t hpu_csr_read32(uintptr_t address) {
    return csr_read(address);
}

#endif
