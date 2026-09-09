#include <hpu/result.h>
#include <hpu/it_v2.h>
#include <hpu/completion.h>

/*
 * 测试点：IT-CFG-003
 * 目的：窗口内地址与传输长度换算。
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
    static const struct {
        unsigned source;
        unsigned output;
        unsigned lines;
        const char *point;
    } ranges[] = {
        {LINE_A, WINDOW_LINES - 1U, 1U, "first-to-last-one-line"},
        {LINE_A + 3U, 320U, 3U, "middle-three-lines"},
        {LINE_A, WINDOW_LINES - POLY_LINES, POLY_LINES, "last-legal-poly"}
    };
    const char *phase = "start";
    case_start(__FILE__);

    /* 每个范围是一段独立完整程序；末尾一次 PSYNC，再比较数据和非输出区域。 */
    for (unsigned round = 0U; round < sizeof(ranges) / sizeof(ranges[0]); ++round) {
        const unsigned source = ranges[round].source;
        const unsigned output = ranges[round].output;
        const unsigned lines = ranges[round].lines;

        phase = "range-data";
        printf("[HPU][RANGE] round=%u point=%s source=%u output=%u lines=%u "
               "words=%u window=[0,%u)\n", round, ranges[round].point,
               source, output, lines, lines * WORDS_PER_LINE, WINDOW_LINES);
        if (v2_prepare(0U, MOD_Q0, MOD_Q1) != 0 ||
            v2_allow_output(output, lines) != 0)
            return failure(__LINE__, phase);

        phase = "range-config";
        printf("[HPU][CONFIG] expected_base=0x%lx expected_lines=%u\n",
               (unsigned long)MEM_BASE, WINDOW_LINES);
        csr_write(CSR_FAULT, FAULT_VALID);
        csr_write(CSR_IRQ, IRQ_LEVEL);
        csr_write(CSR_IRQ, 0U);
        csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
        csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
        csr_write(CSR_SIZE_LO, WINDOW_LINES);
        csr_write(CSR_SIZE_HI, 0U);
        if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
            expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_C(0xff)) != 0 ||
            expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0 ||
            expect_csr(CSR_SIZE_HI, 0U, 1U) != 0)
            return failure(__LINE__, phase);
        csr_write(CSR_COMMIT, COMMIT);
        if (wait_window(1) != 0 || check_status() != 0)
            return failure(__LINE__, phase);

        phase = "range-dma";
        printf("[HPU][ISSUE] p0 DLOAD line=%u count=%u -> "
               "DSTORE line=%u count=%u -> PSYNC\n", source, lines, output, lines);
        if (dload(P0, source, lines) != 0 ||
            dstore_release(P0, output, lines) != 0)
            return failure(__LINE__, phase);
        psync();
        phase = "range-complete";
        if (wait_irq() != 0 || completion_clear() != 0 || check_status() != 0)
            return failure(__LINE__, phase);

        /* golden 读取 CPU 影子而非现场源 DDR；源/guard 被污染也不能蒙混通过。 */
        phase = "range-golden";
        if (v2_check_words(phase, output, v2_expected(source),
                           lines * WORDS_PER_LINE, MOD_Q0) != 0 ||
            v2_check_memory("range-readonly-and-guard") != 0)
            return failure(__LINE__, phase);
        printf("[HPU][RANGE-PASS] round=%u words=%u readonly-and-guard=pass\n",
               round, lines * WORDS_PER_LINE);
    }
    printf("[HPU][SW-CHECK-PASS] monitor=needs-monitor; "
           "AXI address/length and window-external side effects not proved by C\n");
    return case_pass(__FILE__);
}
