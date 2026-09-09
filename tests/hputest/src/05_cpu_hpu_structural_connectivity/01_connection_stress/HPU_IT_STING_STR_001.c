#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>

/*
 * 测试点：IT-STR-001
 * 目的：提供两轮确定性的通道回环样例，供外部 STING/反压平台接入。
 * 本文件不是 STING 随机生成器，没有随机命令序列或 replay seed，不宣称随机覆盖完成。
 * 第一轮 p0/A -> OUT，第二轮 p1/B -> OUT_B；每轮一次末尾 PSYNC。
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
    printf("[HPU][STING-STR001][SCOPE] deterministic adapter sample, NOT a STING generator; "
           "random/replay/ready/CDC evidence must come from the external harness\n");

    for (unsigned round = 0U; round < 2U; ++round) {
        const unsigned object = round == 0U ? P0 : P1;
        const unsigned source_line = round == 0U ? LINE_A : LINE_B;
        const unsigned output_line = round == 0U ? LINE_OUT : LINE_OUT_B;
        const uint32_t *expected;
        int rc;

        printf("[HPU][STING-STR001][PREPARE] round=%u profile=%u "
               "object=p%u source-line=%u output-line=%u count=%u\n",
               round, round, object, source_line, output_line, POLY_LINES);
        if (v2_prepare(round, MOD_Q0, MOD_Q1) != 0)
            return failure(__LINE__, "prepare");
        expected = v2_expected(source_line);
        if (expected == NULL)
            return failure(__LINE__, "input-shadow");
        if (v2_allow_output(output_line, POLY_LINES) != 0)
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

        printf("[HPU][STING-STR001][ISSUE] round=%u DLOAD p%u -> "
               "DSTORE release p%u -> terminal PSYNC\n", round, object, object);
        if (dload(object, source_line, POLY_LINES) != 0)
            return failure(__LINE__, "dload");
        if (dstore_release(object, output_line, POLY_LINES) != 0)
            return failure(__LINE__, "dstore");
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

        printf("[HPU][STING-STR001][CHECK] round=%u compare immutable input and non-output DDR\n", round);
        if (v2_check_words("STING-STR001-loopback", output_line, expected,
                           POLY_WORDS, MOD_Q0) != 0 ||
            v2_check_memory("STING-STR001-readonly-guard") != 0)
            return failure(__LINE__, "data-or-guard");
        printf("[HPU][STING-STR001][ROUND-PASS] round=%u deterministic-software-only\n", round);
    }
    return case_pass(__FILE__);
}
