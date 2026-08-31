#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>
#include <hpu/sync.h>

/*
 * 目的：用 MMIO 轮询完成 DDR -> HPU -> DDR 回环，并逐项自检。
 * 输出区先写 poison，避免 DSTORE 没执行时误把旧数据当成正确结果。
 */
int main(void) {
    uint32_t status = 0U;
    unsigned timeout;

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

    fixture_copy(LINE_A, RNS_A);
    fixture_poison();
    if (dload(P0, LINE_A, RNS_LINES) != 0) return 1;
    if (dstore(P0, LINE_OUT, RNS_LINES) != 0) return 1;
    psync();

    /* CPU 不开中断，只轮询 MMIO 完成电平。 */
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) return 1;
        if ((csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U) break;
    }
    if (timeout == TIMEOUT || (status & STATUS_BUSY) != 0U) return 1;

    csr_write(CSR_IRQ, IRQ_LEVEL);
    for (timeout = 0U; timeout < TIMEOUT / 16U; ++timeout) {
        if ((csr_read(CSR_IRQ) & IRQ_LEVEL) == 0U) break;
    }
    csr_write(CSR_IRQ, 0U);
    if (timeout == TIMEOUT / 16U) return 1;

    /* invalidate 后逐个比较 4096 个系数；任何不一致都 return 1。 */
    if (check_loopback() != 0) return 1;
    return 0;
}
