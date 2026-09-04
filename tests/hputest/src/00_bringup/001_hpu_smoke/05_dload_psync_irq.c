#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/irq.h>
#include <hpu/layout.h>
#include <hpu/sync.h>

/*
 * 目的：验证 DLOAD 后的 terminal PSYNC 能产生完成中断。
 * 与 04 的区别是本用例先做真实 64-line DMA，再等待同一中断路径。
 */
int main(void) {
    case_start(__FILE__);
    uint32_t status = 0U;
    unsigned timeout;
    int rc;

    if (fixture_validate() != 0) return case_fail(__FILE__, __LINE__);

    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, SMOKE_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (csr_read(CSR_BASE_LO) != (uint32_t)MEM_BASE) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_BASE_HI) != (uint32_t)(MEM_BASE >> 32U)) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_SIZE_LO) != SMOKE_LINES) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_SIZE_HI) != 0U) return case_fail(__FILE__, __LINE__);

    csr_write(CSR_COMMIT, COMMIT);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) return case_fail(__FILE__, __LINE__);
        if ((status & STATUS_VALID) != 0U) break;
    }
    if (timeout == TIMEOUT || (status & STATUS_BUSY) != 0U) return case_fail(__FILE__, __LINE__);

    fixture_copy(LINE_A, RNS_A);
    if (irq_open() != 0) return case_fail(__FILE__, __LINE__);
    if (dload(P0, LINE_A, RNS_LINES) != 0) {
        irq_close();
        return case_fail(__FILE__, __LINE__);
    }
    psync();
    rc = irq_wait();
    irq_close();
    if (rc != 0) return case_fail(__FILE__, __LINE__);

    status = csr_read(CSR_STATUS);
    if ((status & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) !=
        STATUS_VALID)
        return case_fail(__FILE__, __LINE__);
    if ((csr_read(CSR_FAULT) & FAULT_VALID) != 0U) return case_fail(__FILE__, __LINE__);
    if ((csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U) return case_fail(__FILE__, __LINE__);
    return case_pass(__FILE__);
}
