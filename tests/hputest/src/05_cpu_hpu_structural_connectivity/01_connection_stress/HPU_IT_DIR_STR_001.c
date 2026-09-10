#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>
#include <hpu/report.h>

/*
 * 测试点：IT-STR-001
 * 目的：提供两个对象的数据回环和计算/控制通道通知，供 IT 施加反压并观察 CDC。
 * 逻辑 custom0 现使用物理 opcode 0x5B；DMA custom1 仍使用 0x2B。
 * 软件只检查完成、回环结果及窗口保护；ready/payload 稳定和跨域时序必须由 monitor 判定。
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
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    printf("[HPU][STR001][SCOPE] software checks loopback/guard only; "
           "ready/backpressure/CDC coverage requires IT monitor\n");

    for (unsigned profile = 0U; profile < 2U; ++profile) {
        const uint32_t *a;
        const uint32_t *b;
        int rc;

        printf("[HPU][STR001][PREPARE] profile=%u words=%u "
               "p0:line%u->%u p1:line%u->%u count=%u window-lines=%u\n",
               profile, POLY_WORDS, LINE_A, LINE_OUT, LINE_B, LINE_OUT_B,
               POLY_LINES, WINDOW_LINES);
        if (v2_prepare(profile, MOD_Q0, MOD_Q1) != 0)
            return failure(__LINE__, "prepare");
        a = v2_expected(LINE_A);
        b = v2_expected(LINE_B);
        if (a == NULL || b == NULL)
            return failure(__LINE__, "input-shadow");
        if (v2_allow_output(LINE_OUT, POLY_LINES) != 0 ||
            v2_allow_output(LINE_OUT_B, POLY_LINES) != 0)
            return failure(__LINE__, "allow-output");
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

        printf("[HPU][STR001][ISSUE] DLOAD p0/A -> DLOAD p1/B -> "
               "DSTORE p0/OUT -> DSTORE p1/OUT_B -> PSYNC; burst has no printf\n");
        if (dload(P0, LINE_A, POLY_LINES) != 0)
            return failure(__LINE__, "dload-p0");
        if (dload(P1, LINE_B, POLY_LINES) != 0)
            return failure(__LINE__, "dload-p1");
        if (dstore_release(P0, LINE_OUT, POLY_LINES) != 0)
            return failure(__LINE__, "dstore-p0");
        if (dstore_release(P1, LINE_OUT_B, POLY_LINES) != 0)
            return failure(__LINE__, "dstore-p1");
        /* 两个对象均由 DSTORE 释放，不能对它们重复 PFREE。 */
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

        printf("[HPU][STR001][CHECK] profile=%u compare-A/B and all non-output DDR\n", profile);
        if (v2_check_words("STR001-p0-A", LINE_OUT, a, POLY_WORDS, MOD_Q0) != 0 ||
            v2_check_words("STR001-p1-B", LINE_OUT_B, b, POLY_WORDS, MOD_Q0) != 0 ||
            v2_check_memory("STR001-readonly-guard") != 0)
            return failure(__LINE__, "data-or-guard");
        printf("[HPU][STR001][ROUND-PASS] profile=%u software-only; "
               "monitor-evidence-still-required\n", profile);
    }
    return case_pass(__FILE__);
}
