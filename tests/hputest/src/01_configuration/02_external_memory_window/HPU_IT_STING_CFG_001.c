#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/it_v2.h>
#include <hpu/completion.h>

/*
 * 测试点：IT-CFG-002
 * 目的：确定性 BASE/SIZE 写序与偏移激励；STING 随机间隔仍需外部入口。
 * 模式：STING约束随机（P2）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_EXTERNAL_ENTRY
 *   - HPU_REQ_STING
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
    const uint32_t seed = 0x04BEA183u;
    printf("[HPU][DATA] profile=producer seed_tag=0x%x A_line=%u B_line=%u "
           "words=%u q=%u window_base=0x%lx window_lines=%u\n",
           seed, LINE_A, LINE_B, POLY_WORDS, MOD_Q0,
           (unsigned long)MEM_BASE, WINDOW_LINES);
    static const unsigned offsets[] = {0U, 32U, 64U, 16U, 0U};
    unsigned index;

    /* seed 只是回放标签，输入来自固定 producer；STING 激励仍由外部入口提供。 */
    if (v2_prepare(0U, MOD_Q0, MOD_Q1) != 0) return failure(__LINE__, phase);

    phase = "clear-old-events";
    printf("[HPU][CLEAR] FAULT.W1C=0x%x IRQ.W1C=0x%x\n", FAULT_VALID, IRQ_LEVEL);
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    if (expect_csr(CSR_FAULT, 0U, FAULT_VALID) != 0 ||
        expect_csr(CSR_IRQ, 0U, IRQ_LEVEL) != 0)
        return failure(__LINE__, phase);

    /*
     * 偶数轮和奇数轮使用不同 shadow 写序；每轮 COMMIT 前都逐项读回。
     * 这只自检合法软件序列，随机间隔/反压覆盖率不能由 C 代码冒充。
     */
    for (index = 0U; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        uintptr_t base = MEM_BASE + (uintptr_t)offsets[index] * LINE_BYTES;
        uint32_t lines = WINDOW_LINES - offsets[index];

        phase = "ordered-shadow-writes";
        printf("[HPU][CONFIG-ORDER] index=%u seed_tag=0x%x base=0x%lx lines=%u "
               "order=%s random_interval=needs-monitor\n", index, seed,
               (unsigned long)base, lines, (index & 1U) == 0U ? "BASE-SIZE" : "MIXED");
        if ((index & 1U) == 0U) {
            csr_write(CSR_BASE_LO, (uint32_t)base);
            csr_write(CSR_BASE_HI, (uint32_t)(base >> 32U));
            csr_write(CSR_SIZE_LO, lines);
            csr_write(CSR_SIZE_HI, 0U);
        } else {
            csr_write(CSR_SIZE_HI, 0U);
            csr_write(CSR_BASE_HI, (uint32_t)(base >> 32U));
            csr_write(CSR_SIZE_LO, lines);
            csr_write(CSR_BASE_LO, (uint32_t)base);
        }
        if (expect_csr(CSR_BASE_LO, (uint32_t)base, UINT32_MAX) != 0 ||
            expect_csr(CSR_BASE_HI, (uint32_t)(base >> 32U), UINT32_C(0xff)) != 0 ||
            expect_csr(CSR_SIZE_LO, lines, UINT32_MAX) != 0 ||
            expect_csr(CSR_SIZE_HI, 0U, 1U) != 0)
            return failure(__LINE__, phase);

        phase = "commit-window";
        printf("[HPU][COMMIT] base_hi=0x%x base_lo=0x%x size_hi=0x%x size_lo=0x%x\n",
               csr_read(CSR_BASE_HI), csr_read(CSR_BASE_LO),
               csr_read(CSR_SIZE_HI), csr_read(CSR_SIZE_LO));
        csr_write(CSR_COMMIT, COMMIT);
        if (wait_window(1) != 0) return failure(__LINE__, phase);
        if (check_status() != 0) return failure(__LINE__, phase);
    }

    phase = "readonly-and-guard";
    if (v2_check_memory(phase) != 0) return failure(__LINE__, phase);
    printf("[HPU][SW-CHECK-PASS] readonly-and-guard=pass window_lines=%u "
           "monitor=needs-monitor; not AXI/handshake coverage\n", WINDOW_LINES);
    return case_pass(__FILE__);
}
