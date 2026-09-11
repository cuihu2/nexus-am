#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/progress.h>
#include <hpu/stage_vectors.h>

/*
 * 测试点：IT-INS-C0-006
 * 目的：PINTT 单 stage 的 P^-1、物理 loader 和 lazy-scale 蝶形，不是整体 INTT。
 * stage=0/1/11 对应正向 stage=11/10/0；逆表不是简单 omega^-1 等比表。
 * 基础输入来自 producer MM，边界输入由 AM 显式派生；三对象必须不同。
 * 单 stage 不执行 N^-1 归一化或 inverse-twist，逐物理字自检。
 */
int main(void) {
    static uint32_t golden[POLY_WORDS];
    const unsigned stages[] = {0U, 1U, 11U};
    const uint32_t *const tables[] = {intt_twiddle_0, intt_twiddle_1, intt_twiddle_11};
    case_start(__FILE__);
    if (progress_begin(__FILE__, 6U) != 0)
        return case_fail(__FILE__, __LINE__);

    for (unsigned profile = 0U; profile < 2U; ++profile) {
        for (unsigned selected = 0U; selected < 3U; ++selected) {
            if (!subcase_selected(profile * 3U + selected)) continue;
            (void)result_context(__FILE__, profile * 3U + selected);
            phase_mark("prepare");
            const unsigned stage = stages[selected];
            const unsigned data_obj = profile == 0U ? P0 : P2;
            const unsigned twiddle_obj = profile == 0U ? P1 : P3;
            const unsigned output_obj = profile == 0U ? P2 : P0;
            const uint32_t *twiddle = tables[selected];
            const uint32_t *input;
            int rc;

            printf("[HPU][PINTT][ROUND] profile=%u stage=%u q=%u src=p%u "
                   "twiddle=p%u dst=p%u data_words=%u twiddle_words=%u\n",
                   profile, stage, STAGE_MODULUS, data_obj, twiddle_obj, output_obj,
                   POLY_WORDS, STAGE_WORDS);
            printf("[HPU][PINTT][LAYOUT] index=physical-word loader_forward_stage=%u "
                   "twiddle=lazy-scale-batch-lane order=P-inverse-then-butterfly\n", 11U - stage);
            if (v2_prepare(profile, MOD_Q0, MOD_Q1) != 0)
                return case_fail(__FILE__, __LINE__);
            if (v2_copy(LINE_TWIDDLE, twiddle, STAGE_WORDS) != 0)
                return case_fail(__FILE__, __LINE__);
            if (v2_allow_output(LINE_OUT, POLY_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
            phase_mark("software-golden");
            input = v2_expected(LINE_A);
            if (input == NULL || MOD_Q0 != STAGE_MODULUS)
                return case_fail(__FILE__, __LINE__);

            /* 独立 C：先 P^-1，再用生产者 alpha/beta 比例表执行蝶形。 */
            if (stage_golden(input, twiddle, golden, POLY_WORDS,
                             STAGE_MODULUS, stage, 1U) != 0)
                return case_fail(__FILE__, __LINE__);

            printf("[HPU][PINTT][CONFIG] base=0x%lx window_lines=%u "
                   "input_line=%u twiddle_line=%u output_line=%u\n",
                   (unsigned long)MEM_BASE, WINDOW_LINES, LINE_A, LINE_TWIDDLE, LINE_OUT);
            phase_mark("configure");
            csr_write(CSR_FAULT, FAULT_VALID);
            csr_write(CSR_IRQ, IRQ_LEVEL);
            csr_write(CSR_IRQ, 0U);
            csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
            csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
            csr_write(CSR_SIZE_LO, WINDOW_LINES);
            csr_write(CSR_SIZE_HI, 0U);
            if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0)
                return case_fail(__FILE__, __LINE__);
            if (expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0)
                return case_fail(__FILE__, __LINE__);
            if (expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0)
                return case_fail(__FILE__, __LINE__);
            if (expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
                return case_fail(__FILE__, __LINE__);
            csr_write(CSR_COMMIT, COMMIT);
            if (wait_window(1) != 0 || check_status() != 0)
                return case_fail(__FILE__, __LINE__);

            printf("[HPU][PINTT][ISSUE] mod -> data -> inverse-twiddle -> stage=%u -> "
                   "DSTORE -> release twiddle/mod -> terminal PSYNC\n", stage);
            phase_mark("issue");
            if (dload_mod(LINE_MOD, 1U) != 0)
                return case_fail(__FILE__, __LINE__);
            if (pmodld(0U) != 0)
                return case_fail(__FILE__, __LINE__);
            if (dload(data_obj, LINE_A, POLY_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
            if (dload(twiddle_obj, LINE_TWIDDLE, STAGE_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
            if (op_intt(output_obj, data_obj, twiddle_obj, stage) != 0)
                return case_fail(__FILE__, __LINE__);
            if (pfree(data_obj) != 0)
                return case_fail(__FILE__, __LINE__);
            if (dstore_release(output_obj, LINE_OUT, POLY_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
            /* 新版 STG 要求空闲目的槽；释放源对象，DSTORE 只释放目的对象。 */
            if (pfree(twiddle_obj) != 0)
                return case_fail(__FILE__, __LINE__);
            if (pfree(P4) != 0)
                return case_fail(__FILE__, __LINE__);
            psync();
            phase_mark("wait-completion");
            rc = wait_irq();
            if (rc != 0) {
                printf("[HPU][PINTT][FAIL] phase=terminal-psync profile=%u stage=%u rc=%d\n",
                       profile, stage, rc);
                return case_fail(__FILE__, __LINE__);
            }
            rc = completion_clear();
            if (rc != 0) {
                printf("[HPU][PINTT][FAIL] phase=clear-completion stage=%u rc=%d\n", stage, rc);
                return case_fail(__FILE__, __LINE__);
            }
            if (check_status() != 0)
                return case_fail(__FILE__, __LINE__);
            phase_mark("compare-results");
            const int data_rc = v2_check_words("PINTT-single-stage", LINE_OUT, golden,
                               POLY_WORDS, STAGE_MODULUS);
            phase_mark("check-guard");
            const int guard_rc = v2_check_memory("readonly-and-guard");
            if (data_rc != 0 || guard_rc != 0)
                return case_fail(__FILE__, __LINE__);
            phase_mark("round-done");
            printf("[HPU][PINTT][ROUND-PASS] profile=%u stage=%u compared=%u "
                   "readonly-and-guard=pass\n", profile, stage, POLY_WORDS);
        }
    }
    return case_pass(__FILE__);
}
