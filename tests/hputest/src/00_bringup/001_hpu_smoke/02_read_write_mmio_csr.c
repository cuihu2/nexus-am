#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>

/*
 * 目的：验证 HPU 的 MMIO 配置写入、配置提交和状态读取。
 * 当前设计只有 MMIO 寄存器窗口，不构造不存在的 RISC-V HPU CSR。
 * 本用例不发 DMA 或计算指令。
 */
int main(void) {
    case_start(__FILE__);
    uint32_t status = 0U;
    unsigned timeout;

    if (fixture_validate() != 0) return case_fail(__FILE__, __LINE__);

    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);

    /* 四个 shadow 寄存器分别写入、分别读回。 */
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, SMOKE_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (csr_read(CSR_BASE_LO) != (uint32_t)MEM_BASE) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_BASE_HI) != (uint32_t)(MEM_BASE >> 32U)) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_SIZE_LO) != SMOKE_LINES) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_SIZE_HI) != 0U) return case_fail(__FILE__, __LINE__);

    /* COMMIT 是写脉冲，提交结果通过 STATUS.valid 判断。 */
    csr_write(CSR_COMMIT, COMMIT);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) return case_fail(__FILE__, __LINE__);
        if ((status & STATUS_VALID) != 0U) break;
    }
    if (timeout == TIMEOUT) return case_fail(__FILE__, __LINE__);
    if ((status & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) !=
        STATUS_VALID)
        return case_fail(__FILE__, __LINE__);
    if ((csr_read(CSR_FAULT) & FAULT_VALID) != 0U) return case_fail(__FILE__, __LINE__);
    if ((csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U) return case_fail(__FILE__, __LINE__);
    return case_pass(__FILE__);
}
