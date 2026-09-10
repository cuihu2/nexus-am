#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>
#include <hpu/report.h>

/*
 * 测试点：IT-STR-006
 * 目的：八条配置命令之间交错普通 RISC-V 整数运算，检查 CPU 结果和 DDR 不被破坏。
 * 精确缓存满载、请求接受/提交顺序仍需 IT queue/ready/commit monitor，不能由结束值推断。
 * 保留原算法：seed=0x5106，每条命令后 128 次 xorshift，末轮预期值 0xba61d264。
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
    const uint32_t seed = UINT32_C(0x5106);
    const uint32_t expected_cpu = UINT32_C(0xba61d264);
    volatile uint32_t cpu_sink = 0U;
    const uint32_t *modulus;
    uint32_t actual_cpu;
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);

    printf("[HPU][STR006][PREPARE] seed=0x%x commands=8 cpu-steps-per-command=128 "
           "expected-final=0x%x mod-line=%u\n", seed, expected_cpu, LINE_MOD);
    printf("[HPU][STR006][SCOPE] completion/CPU-value/DDR checks only; "
           "queue-full and commit-order require external IT evidence\n");
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

    printf("[HPU][STR006][ISSUE] DLOAD mod -> "
           "(PMODLD + 128 CPU xorshift) x8 -> PFREE mod -> terminal PSYNC\n");
    if (dload_mod(LINE_MOD, 1U) != 0)
        return failure(__LINE__, "dload-mod");
    for (unsigned command = 0U; command < 8U; ++command) {
        uint32_t cpu_value = seed ^ command;

        if (pmodld(0U) != 0) {
            printf("[HPU][STR006][FAIL] phase=pmodld command-index=%u\n", command);
            return failure(__LINE__, "pmodld");
        }
        /* uint32_t 运算保留既有模 2^32 语义，循环内部不打印或额外轮询 MMIO。 */
        for (unsigned cpu_step = 0U; cpu_step < 128U; ++cpu_step) {
            cpu_value ^= cpu_value << 13;
            cpu_value ^= cpu_value >> 17;
            cpu_value ^= cpu_value << 5;
        }
        cpu_sink = cpu_value;
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

    actual_cpu = cpu_sink;
    printf("[HPU][STR006][CHECK] CPU actual=0x%x expected=0x%x; "
           "no permitted DDR outputs\n", actual_cpu, expected_cpu);
    if (actual_cpu != expected_cpu) {
        printf("[HPU][STR006][FAIL] phase=cpu-result actual=0x%x "
               "expected=0x%x seed=0x%x commands=8 steps=128\n",
               actual_cpu, expected_cpu, seed);
        return failure(__LINE__, "cpu-result");
    }
    if (v2_check_words("STR006-modulus", LINE_MOD, modulus, WORDS_PER_LINE, 0U) != 0 ||
        v2_check_memory("STR006-readonly-guard") != 0)
        return failure(__LINE__, "data-or-guard");
    printf("[HPU][STR006][SOFTWARE-PASS] CPU result and memory preserved; "
           "queue/commit coverage still requires monitor evidence\n");
    return case_pass(__FILE__);
}
