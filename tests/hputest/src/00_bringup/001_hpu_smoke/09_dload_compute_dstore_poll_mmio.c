#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>
#include "mm.h"

/*
 * 目的：形成 DLOAD -> PMUL -> DSTORE -> PSYNC 的最小计算闭环，
 * 但不打开CPU中断，而是通过MMIO轮询HPU完成电平。
 * 输入、模表、指令流和golden均来自inline-asm的MM交付包。
 */
int main(void) {
    case_start(__FILE__);
    static const hpu_dma_span_t spans[HPU_PROGRAM_MM_DMA_COUNT] = {
        /* 顺序必须与producer的DMA relocation manifest一致。 */
        {LINE_MOD, 1U},
        {LINE_A, RNS_LINES},
        {LINE_B, RNS_LINES},
        {LINE_OUT, RNS_LINES},
    };
    uint32_t status = 0U;
    unsigned timeout;

    if (fixture_validate() != 0) return case_fail(__FILE__, __LINE__);
    if (fixture_validate_mm() != 0) return case_fail(__FILE__, __LINE__);

    /* 清除上一次运行可能留下的fault和完成电平。 */
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);

    /* 逐项配置并读回HPU窗口，最后再提交。 */
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

    /*
     * producer函数负责DLOAD、PMODLD、PMUL、DSTORE和唯一一次terminal
     * PSYNC；此处不再补发PSYNC，也不配置PLIC。
     */
    if (hpu_program_mm(spans, HPU_PROGRAM_MM_DMA_COUNT) != 0) return case_fail(__FILE__, __LINE__);

    /* CPU只轮询MMIO完成电平，同时持续检查fault。 */
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) return case_fail(__FILE__, __LINE__);
        if ((csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U) break;
    }
    if (timeout == TIMEOUT || (status & STATUS_BUSY) != 0U) return case_fail(__FILE__, __LINE__);

    /* W1C清除完成电平，并确认清除动作实际生效。 */
    csr_write(CSR_IRQ, IRQ_LEVEL);
    for (timeout = 0U; timeout < TIMEOUT / 16U; ++timeout) {
        if ((csr_read(CSR_IRQ) & IRQ_LEVEL) == 0U) break;
    }
    csr_write(CSR_IRQ, 0U);
    if (timeout == TIMEOUT / 16U) return case_fail(__FILE__, __LINE__);

    status = csr_read(CSR_STATUS);
    if ((status & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) !=
        STATUS_VALID)
        return case_fail(__FILE__, __LINE__);

    /* HPU输出、producer golden和C的4096项模乘结果必须全部相同。 */
    if (check_pmul() != 0) return case_fail(__FILE__, __LINE__);
    return case_pass(__FILE__);
}
