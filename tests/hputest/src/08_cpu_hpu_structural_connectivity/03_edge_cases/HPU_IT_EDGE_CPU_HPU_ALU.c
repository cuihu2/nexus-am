#include <hpu/edge.h>
#include <hpu/result.h>
#include <hpu/log.h>
#include <hpu/report.h>

/*
 * 测试点：HPU_IT_EDGE_CPU_HPU_ALU
 * 目的：16条算术HPU命令与RV64移位/XOR、CPU保存值混合，分别自检。
 * 1line/64系数；CPU纯C参考先计算，不用HPU返回值反推golden。
 * 不在发令循环里打印或读MMIO；不以软件PASS替代提交/反压monitor。
 */
int main(void) {
    enum { commands = 16 };
    uint32_t golden[WORDS_PER_LINE];
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    trace_phase(TRACE_PREPARE);
    if (v2_prepare(1U, MOD_Q0, MOD_Q1) != 0 || v2_allow_output(LINE_OUT, 1U) != 0)
        return edge_failure(__FILE__, __LINE__, "prepare");
    const uint32_t *a = v2_expected(LINE_A), *b = v2_expected(LINE_B);
    if (a == NULL || b == NULL) return edge_failure(__FILE__, __LINE__, "shadow");

    const uint32_t seed = UINT32_C(0x08110001);
    uint32_t expected_cpu = seed, actual_cpu = seed;
    volatile uint32_t cpu_sink = 0U;
    for (unsigned command = 0; command < commands; ++command)
        expected_cpu = edge_reference_step(expected_cpu ^ command);
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
    __asm__ volatile(".global edge_mix_begin\nedge_mix_begin:" : : : "memory");
    for (unsigned command = 0; command < commands; ++command) {
        actual_cpu = edge_cpu_step(actual_cpu ^ command);
        cpu_sink = actual_cpu;
        int rc = (command & 1U) ? op_mul_imm(P0, P0, 7U) : op_add(P0, P0, P1);
        if (rc != 0) return edge_failure(__FILE__, __LINE__, "mixed-issue");
    }
    __asm__ volatile(".global edge_mix_end\nedge_mix_end:" : : : "memory");
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

    if (cpu_sink != expected_cpu)
        return edge_failure(__FILE__, __LINE__, "cpu-alu-result");
    trace_phase(TRACE_DONE);
    return case_pass(__FILE__);
}
