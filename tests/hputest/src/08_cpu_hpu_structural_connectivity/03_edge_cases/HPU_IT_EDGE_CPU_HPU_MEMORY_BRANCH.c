#include <hpu/edge.h>
#include <hpu/result.h>
#include <hpu/log.h>
#include <hpu/report.h>

/* CPU缓存行数据在BSS，不位于HPU的DDR window中。 */
static volatile uint32_t cpu_scratch[8];

/*
 * 测试点：HPU_IT_EDGE_CPU_HPU_MEMORY_BRANCH
 * 目的：RV整数运算、volatile load/store和条件分支选择HPU加/乘，检查两端结果。
 * 参考scratch/分支路径在发令前独立计算；实际数组逐项检查，不只看末值。
 * 1line/64系数、16条被执行HPU命令；错误路径副作用/提交需配合IT监视。
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

    const uint32_t seed = UINT32_C(0x08120001);
    uint32_t expected_cpu = seed, actual_cpu = seed;
    uint32_t scratch_ref[8] = {0U}, choices[commands];
    unsigned additions = 0U;
    for (unsigned i = 0; i < 8; ++i) cpu_scratch[i] = 0U;
    for (unsigned command = 0; command < commands; ++command) {
        expected_cpu = edge_reference_memory(expected_cpu, command, scratch_ref, &choices[command]);
        additions += choices[command];
    }
    if (additions == 0U || additions == commands)
        return edge_failure(__FILE__, __LINE__, "reference-must-cover-both-branches");
    for (unsigned word = 0; word < WORDS_PER_LINE; ++word) {
        uint64_t value = a[word];
        for (unsigned command = 0; command < commands; ++command)
            value = choices[command] ? (value + b[word]) % MOD_Q0 : value * 7U % MOD_Q0;
        golden[word] = (uint32_t)value;
    }
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
        unsigned index = (actual_cpu >> 3) & 7U;
        cpu_scratch[index] = actual_cpu ^ (UINT32_C(0x9e3779b9) * (command + 1U));
        uint32_t loaded = cpu_scratch[index];  /* 真实load，不能用已知保存值替代。 */
        int rc;
        if ((loaded & 1U) != 0U) rc = op_add(P0, P0, P1);
        else rc = op_mul_imm(P0, P0, 7U);
        if (rc != 0) return edge_failure(__FILE__, __LINE__, "mixed-branch-issue");
        actual_cpu = loaded ^ cpu_scratch[(index + 1U) & 7U];
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

    if (actual_cpu != expected_cpu)
        return edge_failure(__FILE__, __LINE__, "cpu-branch-result");
    for (unsigned i = 0; i < 8; ++i)
        if (cpu_scratch[i] != scratch_ref[i])
            return edge_failure(__FILE__, __LINE__, "cpu-memory-result");
    trace_phase(TRACE_DONE);
    return case_pass(__FILE__);
}
