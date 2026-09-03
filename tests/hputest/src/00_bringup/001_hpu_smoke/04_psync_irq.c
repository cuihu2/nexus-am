#include <hpu/csr.h>
#include <hpu/fixture.h>
#include <hpu/irq.h>
#include <hpu/layout.h>
#include <hpu/sync.h>

/*
 * 目的：单独验证空闲 PSYNC 能触发 CPU 外部中断。
 * 中断处理函数必须 claim source 257、清 HPU 完成电平并完成 PLIC claim。
 */
int main(void) {
    uint32_t status = 0U;
    unsigned timeout;
    int rc;

    if (fixture_validate() != 0) return 1;

    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, SMOKE_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (csr_read(CSR_BASE_LO) != (uint32_t)MEM_BASE) return 1;
    if (csr_read(CSR_BASE_HI) != (uint32_t)(MEM_BASE >> 32U)) return 1;
    if (csr_read(CSR_SIZE_LO) != SMOKE_LINES) return 1;
    if (csr_read(CSR_SIZE_HI) != 0U) return 1;

    csr_write(CSR_COMMIT, COMMIT);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) return 1;
        if ((status & STATUS_VALID) != 0U) break;
    }
    if (timeout == TIMEOUT || (status & STATUS_BUSY) != 0U) return 1;

    if (irq_open() != 0) return 1;
    psync();
    rc = irq_wait();
    irq_close();
    if (rc != 0) return 1;

    status = csr_read(CSR_STATUS);
    if ((status & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) !=
        STATUS_VALID)
        return 1;
    if ((csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U) return 1;
    return 0;
}
