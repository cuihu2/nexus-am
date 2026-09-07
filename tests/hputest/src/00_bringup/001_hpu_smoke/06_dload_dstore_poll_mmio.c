#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>

/*
 * 目的：仅轮询 MMIO STATUS，完成 DDR -> HPU -> DDR 回环并逐项自检。
 * 不发 PSYNC、不访问 IRQ；两次 DMA 分开确认忙到空闲，避免混淆完成边界。
 * 输出区先写 poison，避免 DSTORE 没执行时误把旧数据当成正确结果。
 */
int main(void) {
    case_start(__FILE__);
    uint32_t status = 0U;
    uint32_t value, fault = 0U;
    unsigned timeout;
    int saw_busy = 0;
    int rc;

    rc = fixture_validate();
    if (rc != 0) {
        printf("[HPU][CHECK][FAIL] stage=fixture reason=invalid_input rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }

    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, SMOKE_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    /* 先保存原始读值，再判断和打印；诊断不重复读取寄存器。 */
    value = csr_read(CSR_BASE_LO);
    if (value != (uint32_t)MEM_BASE) {
        printf("[HPU][CHECK][FAIL] stage=init reg=BASE_LO actual=0x%x expected=0x%x\n",
               value, (uint32_t)MEM_BASE);
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_BASE_HI);
    if (value != (uint32_t)(MEM_BASE >> 32U)) {
        printf("[HPU][CHECK][FAIL] stage=init reg=BASE_HI actual=0x%x expected=0x%x\n",
               value, (uint32_t)(MEM_BASE >> 32U));
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_SIZE_LO);
    if (value != SMOKE_LINES) {
        printf("[HPU][CHECK][FAIL] stage=init reg=SIZE_LO actual=0x%x expected=0x%x\n",
               value, SMOKE_LINES);
        return case_fail(__FILE__, __LINE__);
    }
    value = csr_read(CSR_SIZE_HI);
    if (value != 0U) {
        printf("[HPU][CHECK][FAIL] stage=init reg=SIZE_HI actual=0x%x expected=0x0\n",
               value);
        return case_fail(__FILE__, __LINE__);
    }

    csr_write(CSR_COMMIT, COMMIT);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) {
            printf("[HPU][CHECK][FAIL] stage=init reason=status_fault polls=%u status=0x%x\n",
                   timeout + 1U, status);
            return case_fail(__FILE__, __LINE__);
        }
        if ((status & STATUS_VALID) != 0U) break;
    }
    if (timeout == TIMEOUT) {
        printf("[HPU][CHECK][FAIL] stage=init reason=window_not_valid polls=%u status=0x%x\n",
               timeout, status);
        return case_fail(__FILE__, __LINE__);
    }
    if ((status & STATUS_BUSY) != 0U) {
        printf("[HPU][CHECK][FAIL] stage=init reason=busy_after_commit status=0x%x\n", status);
        return case_fail(__FILE__, __LINE__);
    }

    fixture_copy(LINE_A, RNS_A);
    fixture_poison();
    /* 打印放在发指令之前，发出后立即轮询，避免 UART 输出拖过忙阶段。 */
    printf("[HPU][PHASE] DLOAD: poll STATUS busy -> idle\n");
    rc = dload(P0, LINE_A, RNS_LINES);
    if (rc != 0) {
        printf("[HPU][CHECK][FAIL] stage=dload reason=issue_rejected rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }

    /*
     * 先确认 DLOAD 实际进入忙状态，再等它结束，之后才发 DSTORE。
     * 启动前的 BUSY=0 不代表完成；未观察到忙阶段时保守超时报错。
     */
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_VALID) == 0U) {
            printf("[HPU][CHECK][FAIL] stage=dload reason=window_invalid polls=%u status=0x%x\n",
                   timeout + 1U, status);
            return case_fail(__FILE__, __LINE__);
        }
        if ((status & STATUS_FAULT) != 0U) {
            printf("[HPU][CHECK][FAIL] stage=dload reason=status_fault polls=%u status=0x%x\n",
                   timeout + 1U, status);
            return case_fail(__FILE__, __LINE__);
        }
        fault = csr_read(CSR_FAULT);
        if ((fault & FAULT_VALID) != 0U) {
            printf("[HPU][CHECK][FAIL] stage=dload reason=detail_fault polls=%u status=0x%x fault=0x%x\n",
                   timeout + 1U, status, fault);
            return case_fail(__FILE__, __LINE__);
        }
        if ((status & STATUS_BUSY) != 0U) saw_busy = 1;
        else if (saw_busy) break;
    }
    if (timeout == TIMEOUT) {
        printf("[HPU][CHECK][FAIL] stage=dload reason=%s polls=%u status=0x%x fault=0x%x\n",
               saw_busy ? "busy_not_cleared" : "busy_not_observed", timeout, status, fault);
        return case_fail(__FILE__, __LINE__);
    }

    printf("[HPU][PHASE] DLOAD idle; DSTORE: poll STATUS busy -> idle\n");
    saw_busy = 0;
    rc = dstore(P0, LINE_OUT, RNS_LINES);
    if (rc != 0) {
        printf("[HPU][CHECK][FAIL] stage=dstore reason=issue_rejected rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }

    /* DSTORE 必须重新观察忙到空闲，不能沿用 DLOAD 的 saw_busy 或空闲读值。 */
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_VALID) == 0U) {
            printf("[HPU][CHECK][FAIL] stage=dstore reason=window_invalid polls=%u status=0x%x\n",
                   timeout + 1U, status);
            return case_fail(__FILE__, __LINE__);
        }
        if ((status & STATUS_FAULT) != 0U) {
            printf("[HPU][CHECK][FAIL] stage=dstore reason=status_fault polls=%u status=0x%x\n",
                   timeout + 1U, status);
            return case_fail(__FILE__, __LINE__);
        }
        fault = csr_read(CSR_FAULT);
        if ((fault & FAULT_VALID) != 0U) {
            printf("[HPU][CHECK][FAIL] stage=dstore reason=detail_fault polls=%u status=0x%x fault=0x%x\n",
                   timeout + 1U, status, fault);
            return case_fail(__FILE__, __LINE__);
        }
        if ((status & STATUS_BUSY) != 0U) saw_busy = 1;
        else if (saw_busy) break;
    }
    if (timeout == TIMEOUT) {
        printf("[HPU][CHECK][FAIL] stage=dstore reason=%s polls=%u status=0x%x fault=0x%x\n",
               saw_busy ? "busy_not_cleared" : "busy_not_observed", timeout, status, fault);
        return case_fail(__FILE__, __LINE__);
    }

    /* invalidate 后逐个比较 4096 个系数；任何不一致都 return 1。 */
    rc = check_loopback();
    if (rc != 0) {
        printf("[HPU][CHECK][FAIL] stage=loopback reason=compare_failed rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    return case_pass(__FILE__);
}
