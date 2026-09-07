#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/fixture.h>
#include <hpu/irq.h>
#include <hpu/layout.h>
#include <hpu/sync.h>
#include "mm_phases.h"

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
    uint32_t value;
    unsigned timeout;
    int rc;

    rc = fixture_validate();
    if (rc != 0) {
        printf("[HPU][08][FAIL] phase=fixture rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    rc = fixture_validate_mm();
    if (rc != 0) {
        printf("[HPU][08][FAIL] phase=mm-fixture rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }

    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, SMOKE_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    /* 保存本次读回值；失败日志不再次读取寄存器，避免掩盖出错现场。 */
    value = csr_read(CSR_BASE_LO);
    if (value != (uint32_t)MEM_BASE) {
        printf("[HPU][08][FAIL] phase=config reg=BASE_LO actual=0x%x expected=0x%x\n",
               value, (uint32_t)MEM_BASE);
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_BASE_HI);
    if (value != (uint32_t)(MEM_BASE >> 32U)) {
        printf("[HPU][08][FAIL] phase=config reg=BASE_HI actual=0x%x expected=0x%x\n",
               value, (uint32_t)(MEM_BASE >> 32U));
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_SIZE_LO);
    if (value != SMOKE_LINES) {
        printf("[HPU][08][FAIL] phase=config reg=SIZE_LO actual=0x%x expected=0x%x\n",
               value, (uint32_t)SMOKE_LINES);
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_SIZE_HI);
    if (value != 0U) {
        printf("[HPU][08][FAIL] phase=config reg=SIZE_HI actual=0x%x expected=0\n",
               value);
        return case_fail(__FILE__, __LINE__);
    }

    csr_write(CSR_COMMIT, COMMIT);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) {
            printf("[HPU][08][FAIL] phase=commit reason=fault status=0x%x polls=%u\n",
                   status, timeout);
            return case_fail(__FILE__, __LINE__);
        }
        if ((status & STATUS_VALID) != 0U) break;
    }
    if (timeout == TIMEOUT) {
        printf("[HPU][08][FAIL] phase=commit reason=timeout status=0x%x polls=%u\n",
               status, timeout);
        return case_fail(__FILE__, __LINE__);
    }
    if ((status & STATUS_BUSY) != 0U) {
        printf("[HPU][08][FAIL] phase=commit reason=busy status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }

    fixture_copy_mod();
    fixture_copy(LINE_A, RNS_A);
    fixture_copy(LINE_B, RNS_B);
    fixture_poison();

    rc = irq_open();
    if (rc != 0) {
        printf("[HPU][08][FAIL] phase=irq-open rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }

    /* 手册0.4：模表DLOAD后先同步。两个阶段保留producer的机器码和x10/x11绑定。 */
    printf("[HPU][08][PHASE] issue mod-table DLOAD -> PSYNC; wait IRQ\n");
    rc = mm_load_mod(spans, HPU_PROGRAM_MM_DMA_COUNT);
    if (rc != 0) {
        irq_close();
        printf("[HPU][08][FAIL] phase=mod-load rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    psync();
    /* handler清电平并complete第一轮PLIC；重新准备标志后才允许第二轮。 */
    rc = irq_wait();
    if (rc != 0) {
        irq_close();
        printf("[HPU][08][FAIL] phase=mod-irq-wait rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    rc = irq_rearm();
    if (rc != 0) {
        irq_close();
        printf("[HPU][08][FAIL] phase=compute-irq-rearm rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    printf("[HPU][08][PHASE] mod-table synchronized; issue PMODLD -> DLOAD -> PMUL -> DSTORE -> PSYNC\n");

    /* 第二阶段从PMODLD开始，包含输入DLOAD、PMUL、DSTORE和末尾PSYNC。 */
    rc = mm_compute(spans, HPU_PROGRAM_MM_DMA_COUNT);
    if (rc != 0) {
        irq_close();
        printf("[HPU][08][FAIL] phase=mm-compute rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    rc = irq_wait();
    irq_close();
    if (rc != 0) {
        printf("[HPU][08][FAIL] phase=compute-irq-wait rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    status = csr_read(CSR_STATUS);
    if ((status & STATUS_VALID) == 0U) {
        printf("[HPU][08][FAIL] phase=final-status reason=window-invalid status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }
    if ((status & STATUS_BUSY) != 0U) {
        printf("[HPU][08][FAIL] phase=final-status reason=busy status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }
    if ((status & STATUS_FAULT) != 0U) {
        printf("[HPU][08][FAIL] phase=final-status reason=fault status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_IRQ);
    if ((value & IRQ_LEVEL) != 0U) {
        printf("[HPU][08][FAIL] phase=final-irq reason=level-not-cleared irq=0x%x\n", value);
        return case_fail(__FILE__, __LINE__);
    }

    printf("[HPU][08][PHASE] compute IRQ and status checked; compare data\n");
    /* HPU 输出、producer golden 和 C 的 4096 项模乘结果必须全相同。 */
    rc = check_pmul();
    if (rc != 0) {
        printf("[HPU][08][FAIL] phase=pmul-check rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    return case_pass(__FILE__);
}
