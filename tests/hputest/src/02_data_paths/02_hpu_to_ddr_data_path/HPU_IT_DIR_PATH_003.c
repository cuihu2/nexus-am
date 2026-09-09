#include <hpu/result.h>
#include <hpu/it_v2.h>
#include <hpu/completion.h>

/*
 * 测试点：IT-PATH-003
 * 目的：DDR经DLOAD进入对象并回环验证。
 * 模式：定向数据通路（P0）。
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
    const uint32_t seed = 0u;
    printf("[HPU][DATA] profile=producer seed_tag=0x%x A_line=%u B_line=%u "
           "words=%u q=%u window_base=0x%lx window_lines=%u\n",
           seed, LINE_A, LINE_B, POLY_WORDS, MOD_Q0,
           (unsigned long)MEM_BASE, WINDOW_LINES);
    int rc;

    /* A/B 各 4096×32-bit；OUT_A 已被 poison，不能靠预存相同数据误通过。 */
    if (v2_prepare(0U, MOD_Q0, MOD_Q1) != 0) return failure(__LINE__, phase);
    phase = "clear-old-events";
    printf("[HPU][CLEAR] FAULT.W1C=0x%x IRQ.W1C=0x%x\n", FAULT_VALID, IRQ_LEVEL);
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    if (expect_csr(CSR_FAULT, 0U, FAULT_VALID) != 0 ||
        expect_csr(CSR_IRQ, 0U, IRQ_LEVEL) != 0)
        return failure(__LINE__, phase);

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
    if (check_status() != 0) return failure(__LINE__, phase);

    /* DDR[A] -> producer DLOAD p0 -> producer DSTORE p0 -> DDR[OUT_A]。 */
    if (v2_allow_output(LINE_OUT, POLY_LINES) != 0)
        return failure(__LINE__, phase);
    phase = "ddr-object-ddr-loopback";
    printf("[HPU][ISSUE] DLOAD p0 A=%u count=%u -> DSTORE p0 OUT=%u "
           "-> PSYNC\n", LINE_A, POLY_LINES, LINE_OUT);
    rc = dload(P0, LINE_A, POLY_LINES);
    if (rc != 0) return failure(__LINE__, phase);
    rc = dstore_release(P0, LINE_OUT, POLY_LINES);
    if (rc != 0) return failure(__LINE__, phase);
    phase = "terminal-psync";
    psync();
    if (wait_irq() != 0) return failure(__LINE__, phase);
    if (check_status() != 0) return failure(__LINE__, phase);
    phase = "clear-completion";
    if (completion_clear() != 0) return failure(__LINE__, phase);

    phase = "loopback-golden";
    if (v2_check_words(phase, LINE_OUT, v2_expected(LINE_A),
                       POLY_WORDS, MOD_Q0) != 0)
        return failure(__LINE__, phase);
    phase = "readonly-and-guard";
    if (v2_check_memory(phase) != 0) return failure(__LINE__, phase);
    printf("[HPU][SW-CHECK-PASS] readonly-and-guard=pass window_lines=%u "
           "monitor=needs-monitor; not AXI/handshake coverage\n", WINDOW_LINES);
    return case_pass(__FILE__);
}
