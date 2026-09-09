#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>

/*
 * 测试点：IT-STR-002
 * 目的：向 IT 提供九条连续配置命令，检查八条缓存满载后的受控等待与恢复。
 * CPU 发九条命令不等于硬件队列曾经装满：必须由环境控制 ready/队列释放并记录入队序号。
 * 本用例不写输出 DDR，所有 512 line 都必须保持初始数据；不以软件 PASS 替代队列覆盖。
 */
/* 失败后各读一次现场；三项 MMIO 不是同一原子快照，不覆盖等待函数的首错日志。 */
static int failure(unsigned line, const char *phase) {
    const uint32_t status = csr_read(CSR_STATUS);
    const uint32_t fault = csr_read(CSR_FAULT);
    const uint32_t irq = csr_read(CSR_IRQ);

    printf("[HPU][FAIL] phase=%s post-failure-status=0x%x fault=0x%x irq=0x%x\n",
           phase, status, fault, irq);
    return case_fail(__FILE__, line);
}

int main(void) {
    const unsigned commands = 9U;
    const uint32_t *modulus;
    int rc;
    case_start(__FILE__);

    printf("[HPU][STR002][PREPARE] profile=0 commands=%u queue-target=8+1 "
           "mod-line=%u window-lines=%u\n", commands, LINE_MOD, WINDOW_LINES);
    printf("[HPU][STR002][SCOPE] IT must hold/release ready and observe "
           "queue-full, ninth-request-wait, stable-payload and ordered-recovery\n");
    if (v2_prepare(0U, MOD_Q0, MOD_Q1) != 0)
        return failure(__LINE__, "prepare");
    modulus = v2_expected(LINE_MOD);
    if (modulus == NULL)
        return failure(__LINE__, "modulus-shadow");
    /* 数据准备不隐含 CSR 操作，窗口各字段在 main 中明确配置并读回。 */
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, WINDOW_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0)
        return failure(__LINE__, "config-base-lo");
    if (expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0)
        return failure(__LINE__, "config-base-hi");
    if (expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0)
        return failure(__LINE__, "config-size-lo");
    if (expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return failure(__LINE__, "config-size-hi");
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0)
        return failure(__LINE__, "commit-wait");
    if (check_status() != 0)
        return failure(__LINE__, "configured-status");

    printf("[HPU][STR002][ISSUE] DLOAD mod -> PMODLD(0) x9 -> "
           "PFREE mod -> PSYNC; no printf inside the nine-command burst\n");
    if (dload_mod(LINE_MOD, 1U) != 0)
        return failure(__LINE__, "dload-mod");
    /* 只在发令前打印，避免串口开销在九条命令之间人为排空队列。 */
    for (unsigned command = 0U; command < commands; ++command) {
        if (pmodld(0U) != 0) {
            printf("[HPU][STR002][FAIL] phase=pmodld command-index=%u\n", command);
            return failure(__LINE__, "pmodld-burst");
        }
    }
    if (pfree(P4) != 0)
        return failure(__LINE__, "free-mod");
    /* 每段完整程序只发这一次 PSYNC；以下等待/清除函数不会再发指令。 */
    psync();
    rc = wait_irq();
    if (rc != 0) {
        printf("[HPU][FAIL] phase=terminal-psync rc=%d\n", rc);
        return failure(__LINE__, "terminal-psync");
    }
    if (completion_clear() != 0)
        return failure(__LINE__, "completion-clear");
    if (check_status() != 0)
        return failure(__LINE__, "final-status");

    printf("[HPU][STR002][CHECK] no permitted DDR outputs; compare mod and full-window guard\n");
    if (v2_check_words("STR002-modulus", LINE_MOD, modulus, WORDS_PER_LINE, 0U) != 0 ||
        v2_check_memory("STR002-readonly-guard") != 0)
        return failure(__LINE__, "data-or-guard");
    printf("[HPU][STR002][SOFTWARE-PASS] completion/memory checks passed; "
           "8-full/9-wait is NOT established without queue/ready monitor evidence\n");
    return case_pass(__FILE__);
}
