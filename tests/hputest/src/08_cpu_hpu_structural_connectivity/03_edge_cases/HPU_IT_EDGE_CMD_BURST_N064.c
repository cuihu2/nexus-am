#include <hpu/edge.h>
#include <hpu/result.h>
#include <hpu/log.h>
#include <hpu/report.h>

/*
 * 测试点：HPU_IT_EDGE_CMD_BURST_N064
 * 目的：连续64条HPU算术命令；PADD与PMUL(7)交替，验证数量与顺序。
 * 仅1line/64系数，独立ELF只运行一次；队列满载及反压仍需IT monitor。
 * 指令word来自inline-asm编码器；burst中无C分支、函数调用、MMIO或printf。
 */
int main(void) {
    enum { commands = 64 };
    uint32_t golden[WORDS_PER_LINE];
    register uintptr_t sentinel_a __asm__("s2") = UINT64_C(0x123456789abcdef0);
    register uintptr_t sentinel_b __asm__("s3") = UINT64_C(0xfedcba9876543210);
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    trace_phase(TRACE_PREPARE);
    if (v2_prepare(1U, MOD_Q0, MOD_Q1) != 0 || v2_allow_output(LINE_OUT, 1U) != 0)
        return edge_failure(__FILE__, __LINE__, "prepare");
    const uint32_t *a = v2_expected(LINE_A), *b = v2_expected(LINE_B);
    if (a == NULL || b == NULL) return edge_failure(__FILE__, __LINE__, "shadow");
    for (unsigned word = 0; word < WORDS_PER_LINE; ++word)
        golden[word] = edge_reference_burst(a[word], b[word], commands, MOD_Q0);
    /* shadow配置和回读保持寄存器粒度，不隐藏在初始化函数中。 */
    trace_phase(TRACE_CONFIGURE);
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, WINDOW_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return edge_failure(__FILE__, __LINE__, "configure");
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return edge_failure(__FILE__, __LINE__, "commit");
    if (dload_mod(LINE_MOD, 1U) != 0 || pmodld(0U) != 0 ||
        dload(P0, LINE_A, 1U) != 0 || dload(P1, LINE_B, 1U) != 0)
        return edge_failure(__FILE__, __LINE__, "load");

    trace_phase(TRACE_ISSUE);
    LOG_DEBUG("[HPU][EDGE] burst=%u words=%u\n", commands, WORDS_PER_LINE);
    /* 区间符号供反汇编/波形定位；汇编直接展开，奇数末尾补一条PADD。 */
    __asm__ volatile(
        ".global edge_burst_begin\nedge_burst_begin:\n"
        ".rept %2\n.word %4\n.word %5\n.endr\n"
        ".rept %3\n.word %4\n.endr\n"
        ".global edge_burst_end\nedge_burst_end:\n"
        : "+r"(sentinel_a), "+r"(sentinel_b)
        : "i"(commands / 2U), "i"(commands % 2U),
          "i"((uint32_t)HPU_INSN_PADD_P0_P0_P1),
          "i"((uint32_t)HPU_INSN_PMUL_IMM7_P0_P0)
        : "memory");
    if (sentinel_a != UINT64_C(0x123456789abcdef0) ||
        sentinel_b != UINT64_C(0xfedcba9876543210))
        return edge_failure(__FILE__, __LINE__, "cpu-registers");
    /* p0由DSTORE释放；p1及模表显式释放，不重复释放p0。 */
    if (dstore_release(P0, LINE_OUT, 1U) != 0 || pfree(P1) != 0 || pfree(P4) != 0)
        return edge_failure(__FILE__, __LINE__, "store-release");
    psync(); /* 完整程序末尾唯一PSYNC，不在内部阶段插入。 */
    trace_phase(TRACE_WAIT);
    if (edge_wait(EDGE_WAIT_CYCLES) != 0 || completion_clear() != 0 || check_status() != 0)
        return edge_failure(__FILE__, __LINE__, "completion");
    trace_phase(TRACE_COMPARE);
    if (v2_check_words("edge-result", LINE_OUT, golden, WORDS_PER_LINE, MOD_Q0) != 0 ||
        v2_check_memory("edge-readonly-guard") != 0)
        return edge_failure(__FILE__, __LINE__, "result-or-guard");

    trace_phase(TRACE_DONE);
    return case_pass(__FILE__);
}
