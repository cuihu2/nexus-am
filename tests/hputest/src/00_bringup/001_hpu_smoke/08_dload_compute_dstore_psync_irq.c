#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/fixture.h>
#include <hpu/irq.h>
#include <hpu/layout.h>
#include "mm.h"

/*
 * 目的：形成 DLOAD -> PMUL -> DSTORE -> PSYNC 的最小计算闭环。
 * 输入、模表、指令流和 golden 都来自 inline-asm 的 MM 交付包。
 * 完成方式固定为 PSYNC 中断，不使用不存在的 RISC-V HPU CSR。
 */
int main(void) {
    case_start(__FILE__);
    static const hpu_dma_span_t spans[HPU_PROGRAM_MM_DMA_COUNT] = {
        /* 顺序必须与 producer 的 DMA relocation manifest 一致。 */
        {LINE_MOD, 1U},
        {LINE_A, RNS_LINES},
        {LINE_B, RNS_LINES},
        {LINE_OUT, RNS_LINES},
    };
    uint32_t status = 0U;
    unsigned timeout;
    int rc;

    if (fixture_validate() != 0) return case_fail(__FILE__, __LINE__);
    if (fixture_validate_mm() != 0) return case_fail(__FILE__, __LINE__);

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

    fixture_copy_mod();
    fixture_copy(LINE_A, RNS_A);
    fixture_copy(LINE_B, RNS_B);
    fixture_poison();

    if (irq_open() != 0) return case_fail(__FILE__, __LINE__);

    /*
     * producer 函数会在每条 custom1 前写 x10/x11，并且内部只发一次
     * terminal PSYNC，因此 main 不再额外补发 PSYNC。
     */
    rc = hpu_program_mm(spans, HPU_PROGRAM_MM_DMA_COUNT);
    if (rc == 0) rc = irq_wait();
    irq_close();
    if (rc != 0) return case_fail(__FILE__, __LINE__);

    status = csr_read(CSR_STATUS);
    if ((status & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) !=
        STATUS_VALID)
        return case_fail(__FILE__, __LINE__);
    if ((csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U) return case_fail(__FILE__, __LINE__);

    /* HPU 输出、producer golden 和 C 的 4096 项模乘结果必须全相同。 */
    if (check_pmul() != 0) return case_fail(__FILE__, __LINE__);
    return case_pass(__FILE__);
}
