#include <hpu/csr.h>
#include <hpu/layout.h>
#include <hpu/result.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_02_READ_CSR_STATUS";
    uint32_t base_lo;
    uint32_t base_hi;
    uint32_t size_lo;
    uint32_t size_hi;
    uint32_t status;
    uint32_t fault;
    uint32_t irq;
    unsigned timeout;

    hpu_test_begin(case_id, "read-csr-status");

    /* 写入 BASE/SIZE shadow，然后逐项读回。 */
    hpu_csr_write(HPU_CSR_FAULT, 1U);
    hpu_csr_write(HPU_CSR_IRQ, 1U);
    hpu_csr_write(HPU_CSR_IRQ, 0U);
    hpu_csr_write(HPU_CSR_BASE_LO, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write(HPU_CSR_BASE_HI, 0U);
    hpu_csr_write(HPU_CSR_SIZE_LO, HPU_WINDOW_LINES);
    hpu_csr_write(HPU_CSR_SIZE_HI, 0U);

    base_lo = hpu_csr_read(HPU_CSR_BASE_LO);
    base_hi = hpu_csr_read(HPU_CSR_BASE_HI);
    size_lo = hpu_csr_read(HPU_CSR_SIZE_LO);
    size_hi = hpu_csr_read(HPU_CSR_SIZE_HI);
    if (base_lo != (uint32_t)HPU_MEM_BASE || (base_hi & 0xffU) != 0U ||
        size_lo != HPU_WINDOW_LINES || (size_hi & 1U) != 0U) {
        return hpu_test_end(case_id, 11);
    }

    /* COMMIT 后轮询 STATUS.window_valid。 */
    hpu_csr_write(HPU_CSR_COMMIT, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_WINDOW_VALID) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return hpu_test_end(case_id, 12);

    fault = hpu_csr_read(HPU_CSR_FAULT);
    irq = hpu_csr_read(HPU_CSR_IRQ);
    printf("HPU_SMOKE_CSR base_lo=0x%x base_hi=0x%x size_lo=%u "
           "size_hi=0x%x status=0x%x fault=0x%x irq=0x%x\n",
           base_lo, base_hi, size_lo, size_hi, status, fault, irq);

    if ((status & HPU_STATUS_FAULT_VALID) != 0U || (fault & 1U) != 0U) {
        return hpu_test_end(case_id, 13);
    }
    return hpu_test_end(case_id, 0);
}
