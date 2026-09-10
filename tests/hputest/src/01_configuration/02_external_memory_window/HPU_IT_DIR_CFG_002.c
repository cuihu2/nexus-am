#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/it_v2.h>
#include <hpu/completion.h>

/*
 * 测试点：IT-CFG-002
 * 目的：窗口 A/B 提交后的地址映射；原子生效和无半更新组合由 monitor 验证。
 * 模式：定向配置（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

/* 失败后补充只读 CSR 诊断；各寄存器独立采样，不是原子快照，不读 PLIC claim。 */
static int failure(unsigned source_line, const char *phase) {
    const uint32_t status = csr_read(CSR_STATUS);
    const uint32_t fault = csr_read(CSR_FAULT);
    const uint32_t irq = csr_read(CSR_IRQ);
    const uint32_t base_lo = csr_read(CSR_BASE_LO);
    const uint32_t base_hi = csr_read(CSR_BASE_HI);
    const uint32_t size_lo = csr_read(CSR_SIZE_LO);
    const uint32_t size_hi = csr_read(CSR_SIZE_HI);

    printf("[HPU][FAIL] phase=%s source_line=%u status=0x%x fault=0x%x irq=0x%x\n",
           phase, source_line, status, fault, irq);
    printf("[HPU][FAIL][window-shadow] base_hi=0x%x base_lo=0x%x "
           "size_hi=0x%x size_lo=0x%x\n", base_hi, base_lo, size_hi, size_lo);
    return case_fail(__FILE__, source_line);
}

int main(void) {
    const char *phase = "data-prepare";
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    const uint32_t seed = 0u;
    printf("[HPU][DATA] profile=producer seed_tag=0x%x A_line=%u B_line=%u "
           "words=%u q=%u window_base=0x%lx window_lines=%u\n",
           seed, LINE_A, LINE_B, POLY_WORDS, MOD_Q0,
           (unsigned long)MEM_BASE, WINDOW_LINES);
    enum { WINDOW_B_OFFSET = 256U, WINDOW_B_LINES = 256U };
    uint32_t expected[WORDS_PER_LINE];
    uintptr_t window_b = MEM_BASE +
        (uintptr_t)WINDOW_B_OFFSET * LINE_BYTES;
    unsigned word;
    int rc;

    /*
     * B 窗口 canary 显式由 producer A 首行逐项加 1 派生，每项均不同于 A。
     * 不能直接复制 A：否则 DLOAD 误用旧 BASE、DSTORE 使用新 BASE 时会假通过。
     */
    if (v2_prepare(0U, MOD_Q0, MOD_Q1) != 0) return failure(__LINE__, phase);
    for (word = 0U; word < WORDS_PER_LINE; ++word)
        expected[word] = (uint32_t)(((uint64_t)v2_expected(LINE_A)[word] + 1U) % MOD_Q0);
    if (v2_copy(WINDOW_B_OFFSET, expected, WORDS_PER_LINE) != 0)
        return failure(__LINE__, phase);
    if (v2_allow_output(WINDOW_B_OFFSET + 64U, 1U) != 0)
        return failure(__LINE__, phase);

    phase = "clear-old-events";
    printf("[HPU][CLEAR] FAULT.W1C=0x%x IRQ.W1C=0x%x\n", FAULT_VALID, IRQ_LEVEL);
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    if (expect_csr(CSR_FAULT, 0U, FAULT_VALID) != 0)
        return failure(__LINE__, phase);

    /* 先提交窗口 A（完整 512 lines），所有 shadow 值逐项读回。 */
    phase = "write-shadow";
    printf("[HPU][CONFIG] expected_base=0x%lx expected_lines=%u\n",
           (unsigned long)MEM_BASE, WINDOW_LINES);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, WINDOW_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_C(0xff)) != 0 ||
        expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, 1U) != 0)
        return failure(__LINE__, phase);
    phase = "commit-window";
    printf("[HPU][COMMIT] base_hi=0x%x base_lo=0x%x size_hi=0x%x size_lo=0x%x\n",
           csr_read(CSR_BASE_HI), csr_read(CSR_BASE_LO),
           csr_read(CSR_SIZE_HI), csr_read(CSR_SIZE_LO));
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0) return failure(__LINE__, phase);

    /* 再提交窗口 B；相同相对 line 现在必须落到物理 line 256 之后。 */
    phase = "write-shadow-B";
    printf("[HPU][CONFIG-B] expected_base=0x%lx expected_lines=%u\n",
           (unsigned long)window_b, WINDOW_B_LINES);
    csr_write(CSR_BASE_LO, (uint32_t)window_b);
    csr_write(CSR_BASE_HI, (uint32_t)(window_b >> 32U));
    csr_write(CSR_SIZE_LO, WINDOW_B_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)window_b, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(window_b >> 32U), UINT32_C(0xff)) != 0 ||
        expect_csr(CSR_SIZE_LO, WINDOW_B_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, 1U) != 0)
        return failure(__LINE__, phase);
    phase = "commit-window";
    printf("[HPU][COMMIT] base_hi=0x%x base_lo=0x%x size_hi=0x%x size_lo=0x%x\n",
           csr_read(CSR_BASE_HI), csr_read(CSR_BASE_LO),
           csr_read(CSR_SIZE_HI), csr_read(CSR_SIZE_LO));
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0) return failure(__LINE__, phase);

    /* producer DLOAD/DSTORE 使用 x10=line、x11=count；相对地址保持不变。 */
    phase = "window-B-loopback";
    printf("[HPU][ISSUE] DLOAD p0 relative_line=0 count=1 -> "
           "DSTORE p0 relative_line=64 physical_line=%u -> PSYNC\n",
           WINDOW_B_OFFSET + 64U);
    rc = dload(P0, 0U, 1U);
    if (rc != 0) return failure(__LINE__, phase);
    rc = dstore_release(P0, 64U, 1U);
    if (rc != 0) return failure(__LINE__, phase);
    phase = "terminal-psync";
    psync();
    if (wait_irq() != 0) return failure(__LINE__, phase);
    if (check_status() != 0) return failure(__LINE__, phase);
    phase = "clear-completion";
    if (completion_clear() != 0) return failure(__LINE__, phase);

    /* CPU 绝对地址 line 320 的 64 个 word 必须等于窗口 B 的 line 0。 */
    phase = "window-B-golden";
    if (v2_check_words(phase, WINDOW_B_OFFSET + 64U, expected,
                       WORDS_PER_LINE, MOD_Q0) != 0)
        return failure(__LINE__, phase);
    phase = "readonly-and-guard";
    if (v2_check_memory(phase) != 0) return failure(__LINE__, phase);
    printf("[HPU][SW-CHECK-PASS] readonly-and-guard=pass window_lines=%u "
           "monitor=needs-monitor; not AXI/handshake coverage\n", WINDOW_LINES);
    return case_pass(__FILE__);
}
