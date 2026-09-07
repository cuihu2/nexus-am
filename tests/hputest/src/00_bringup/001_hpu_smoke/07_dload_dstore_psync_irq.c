#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/irq.h>
#include <hpu/layout.h>
#include <hpu/sync.h>

/*
 * 目的：使用 PSYNC 中断完成 DDR -> HPU -> DDR 回环，并逐项自检。
 * 本例连续发出 DLOAD/DSTORE 后等中断；06 则分步轮询 DMA 状态，不发 PSYNC。
 */
int main(void) {
    case_start(__FILE__);
    uint32_t status = 0U;
    uint32_t value;
    unsigned timeout;
    int rc;

    rc = fixture_validate();
    if (rc != 0) {
        printf("[HPU][07][FAIL] phase=fixture rc=%d\n", rc);
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
        printf("[HPU][07][FAIL] phase=config reg=BASE_LO actual=0x%x expected=0x%x\n",
               value, (uint32_t)MEM_BASE);
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_BASE_HI);
    if (value != (uint32_t)(MEM_BASE >> 32U)) {
        printf("[HPU][07][FAIL] phase=config reg=BASE_HI actual=0x%x expected=0x%x\n",
               value, (uint32_t)(MEM_BASE >> 32U));
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_SIZE_LO);
    if (value != SMOKE_LINES) {
        printf("[HPU][07][FAIL] phase=config reg=SIZE_LO actual=0x%x expected=0x%x\n",
               value, (uint32_t)SMOKE_LINES);
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_SIZE_HI);
    if (value != 0U) {
        printf("[HPU][07][FAIL] phase=config reg=SIZE_HI actual=0x%x expected=0\n",
               value);
        return case_fail(__FILE__, __LINE__);
    }

    csr_write(CSR_COMMIT, COMMIT);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) {
            printf("[HPU][07][FAIL] phase=commit reason=fault status=0x%x polls=%u\n",
                   status, timeout);
            return case_fail(__FILE__, __LINE__);
        }
        if ((status & STATUS_VALID) != 0U) break;
    }
    if (timeout == TIMEOUT) {
        printf("[HPU][07][FAIL] phase=commit reason=timeout status=0x%x polls=%u\n",
               status, timeout);
        return case_fail(__FILE__, __LINE__);
    }
    if ((status & STATUS_BUSY) != 0U) {
        printf("[HPU][07][FAIL] phase=commit reason=busy status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }

    fixture_copy(LINE_A, RNS_A);
    fixture_poison();
    rc = irq_open();
    if (rc != 0) {
        printf("[HPU][07][FAIL] phase=irq-open rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    /* 连续发指令期间不打印，避免串口延迟改变原有命令间隔。 */
    printf("[HPU][07][PHASE] issue DLOAD -> DSTORE -> PSYNC; wait IRQ\n");
    rc = dload(P0, LINE_A, RNS_LINES);
    if (rc != 0) {
        irq_close();
        printf("[HPU][07][FAIL] phase=dload rc=%d object=%u line=%u lines=%u\n",
               rc, (unsigned)P0, (unsigned)LINE_A, (unsigned)RNS_LINES);
        return case_fail(__FILE__, __LINE__);
    }
    rc = dstore(P0, LINE_OUT, RNS_LINES);
    if (rc != 0) {
        irq_close();
        printf("[HPU][07][FAIL] phase=dstore rc=%d object=%u line=%u lines=%u\n",
               rc, (unsigned)P0, (unsigned)LINE_OUT, (unsigned)RNS_LINES);
        return case_fail(__FILE__, __LINE__);
    }
    psync();
    rc = irq_wait();
    irq_close();
    if (rc != 0) {
        printf("[HPU][07][FAIL] phase=loopback-irq-wait rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    status = csr_read(CSR_STATUS);
    if ((status & STATUS_VALID) == 0U) {
        printf("[HPU][07][FAIL] phase=final-status reason=window-invalid status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }
    if ((status & STATUS_BUSY) != 0U) {
        printf("[HPU][07][FAIL] phase=final-status reason=busy status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }
    if ((status & STATUS_FAULT) != 0U) {
        printf("[HPU][07][FAIL] phase=final-status reason=fault status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_IRQ);
    if ((value & IRQ_LEVEL) != 0U) {
        printf("[HPU][07][FAIL] phase=final-irq reason=level-not-cleared irq=0x%x\n", value);
        return case_fail(__FILE__, __LINE__);
    }
    printf("[HPU][07][PHASE] loopback IRQ and status checked; compare data\n");
    rc = check_loopback();
    if (rc != 0) {
        printf("[HPU][07][FAIL] phase=loopback-check rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    return case_pass(__FILE__);
}
