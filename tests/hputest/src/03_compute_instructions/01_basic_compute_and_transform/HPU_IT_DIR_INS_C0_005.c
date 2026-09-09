#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>
#include <hpu/stage_vectors.h>

/*
 * 测试点：IT-INS-C0-005
 * 目的：PNTT 单 stage 的前向蝶形，不是整体 NTT。
 * stage=0/1/11 分别覆盖首级、非平凡 twiddle 和末级配对。
 * 基础输入来自 producer MM；边界输入由 AM 显式派生，使用不同对象 p2/p3。
 * 不添加 pre-twist、bit-reversal 或全变换 golden；只核对本条指令的蝶形。
 */
int main(void) {
    static uint32_t golden[POLY_WORDS];
    const unsigned stages[] = {0U, 1U, 11U};
    const uint32_t *const tables[] = {ntt_twiddle_0, ntt_twiddle_1, ntt_twiddle_11};
    case_start(__FILE__);

    for (unsigned profile = 0U; profile < 2U; ++profile) {
        for (unsigned selected = 0U; selected < 3U; ++selected) {
            const unsigned stage = stages[selected];
            const unsigned data_obj = profile == 0U ? P0 : P2;
            const unsigned twiddle_obj = profile == 0U ? P1 : P3;
            const uint32_t *twiddle = tables[selected];
            const unsigned half = 1U << stage;
            const uint32_t *input;
            unsigned twiddle_index = 0U;
            int rc;

            printf("[HPU][PNTT][ROUND] profile=%u stage=%u q=%u data=p%u "
                   "twiddle=p%u data_words=%u twiddle_words=%u\n",
                   profile, stage, STAGE_MODULUS, data_obj, twiddle_obj,
                   POLY_WORDS, STAGE_WORDS);
            if (v2_prepare(profile, MOD_Q0, MOD_Q1) != 0)
                return case_fail(__FILE__, __LINE__);
            if (v2_copy(LINE_TWIDDLE, twiddle, STAGE_WORDS) != 0)
                return case_fail(__FILE__, __LINE__);
            if (v2_allow_output(LINE_OUT, POLY_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
            input = v2_expected(LINE_A);
            if (input == NULL || MOD_Q0 != STAGE_MODULUS)
                return case_fail(__FILE__, __LINE__);

            /* 独立 C 蝶形：从只读影子取输入，按 group-major 顺序消费真实 twiddle。 */
            for (unsigned begin = 0U; begin < POLY_WORDS; begin += 2U * half) {
                for (unsigned j = 0U; j < half; ++j) {
                    const unsigned even = begin + j;
                    const unsigned odd = even + half;
                    const uint32_t a = input[even];
                    const uint32_t b = (uint32_t)(
                        (uint64_t)input[odd] * twiddle[twiddle_index++] % STAGE_MODULUS);
                    golden[even] = (uint32_t)(((uint64_t)a + b) % STAGE_MODULUS);
                    golden[odd] = a >= b ? a - b : STAGE_MODULUS - (b - a);
                }
            }
            if (twiddle_index != STAGE_WORDS)
                return case_fail(__FILE__, __LINE__);

            printf("[HPU][PNTT][CONFIG] base=0x%lx window_lines=%u "
                   "input_line=%u twiddle_line=%u output_line=%u\n",
                   (unsigned long)MEM_BASE, WINDOW_LINES, LINE_A, LINE_TWIDDLE, LINE_OUT);
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

            printf("[HPU][PNTT][ISSUE] mod -> data -> twiddle -> stage=%u -> "
                   "DSTORE -> release twiddle/mod -> terminal PSYNC\n", stage);
            if (dload_mod(LINE_MOD, 1U) != 0)
                return case_fail(__FILE__, __LINE__);
            if (pmodld(0U) != 0)
                return case_fail(__FILE__, __LINE__);
            if (dload(data_obj, LINE_A, POLY_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
            if (dload(twiddle_obj, LINE_TWIDDLE, STAGE_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
            if (op_ntt(data_obj, twiddle_obj, stage) != 0)
                return case_fail(__FILE__, __LINE__);
            if (dstore_release(data_obj, LINE_OUT, POLY_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
            /* DSTORE 释放数据对象；twiddle 与模表仍需显式释放。 */
            if (pfree(twiddle_obj) != 0)
                return case_fail(__FILE__, __LINE__);
            if (pfree(P4) != 0)
                return case_fail(__FILE__, __LINE__);
            psync();
            rc = wait_irq();
            if (rc != 0) {
                printf("[HPU][PNTT][FAIL] phase=terminal-psync profile=%u stage=%u rc=%d\n",
                       profile, stage, rc);
                return case_fail(__FILE__, __LINE__);
            }
            rc = completion_clear();
            if (rc != 0) {
                printf("[HPU][PNTT][FAIL] phase=clear-completion stage=%u rc=%d\n", stage, rc);
                return case_fail(__FILE__, __LINE__);
            }
            if (check_status() != 0 || v2_check_memory("PNTT-readonly-and-guard") != 0)
                return case_fail(__FILE__, __LINE__);
            if (v2_check_words("PNTT-single-stage", LINE_OUT, golden,
                               POLY_WORDS, STAGE_MODULUS) != 0)
                return case_fail(__FILE__, __LINE__);
            printf("[HPU][PNTT][ROUND-PASS] profile=%u stage=%u compared=%u "
                   "readonly-and-guard=pass\n", profile, stage, POLY_WORDS);
        }
    }
    return case_pass(__FILE__);
}
