#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/progress.h>

/*
 * 测试点：IT-INS-C0-004
 * 目的：PMAC 使用已加载的累加初值，验证模乘加和连续依赖。
 * 基础轮初值 0；边界轮初值 q-1 并连续两次对象 PMAC；
 * 立即数轮初值 1、立即数 255；最后一轮目的与源1同为 p0。
 * golden 由 C 的 64 位乘加独立求模。
 */
int main(void) {
    static uint32_t golden[POLY_WORDS];
    case_start(__FILE__);
    if (progress_begin(__FILE__, 4U) != 0)
        return case_fail(__FILE__, __LINE__);

    for (unsigned variant = 0U; variant < 4U; ++variant) {
        if (!subcase_selected(variant)) continue;
        (void)result_context(__FILE__, variant);
        phase_mark("prepare");
        const unsigned profile = variant == 0U ? 0U : 1U;
        const unsigned immediate_mode = variant == 2U;
        const unsigned alias_source = variant == 3U;
        const unsigned dst = alias_source ? P0 : P2;
        const unsigned repeats = variant == 1U ? 2U : 1U;
        const uint32_t initial = variant == 0U ? 0U :
            variant == 1U ? MOD_Q0 - 1U : 1U;
        const uint32_t *a;
        const uint32_t *b;
        int rc;

        printf("[HPU][PMAC][ROUND] variant=%u profile=%u dst=p%u "
               "accumulator=%s mode=%s repeats=%u words=%u q=%u\n",
               variant, profile, dst, alias_source ? "input-A" : "constant",
               immediate_mode ? "immediate" : "object", repeats,
               POLY_WORDS, MOD_Q0);
        if (!alias_source) printf("[HPU][PMAC][PARAM] initial=%u\n", initial);
        if (immediate_mode) printf("[HPU][PMAC][PARAM] immediate=255\n");
        if (v2_prepare(profile, MOD_Q0, MOD_Q1) != 0)
            return case_fail(__FILE__, __LINE__);
        if (v2_fill(LINE_SCRATCH, initial, POLY_WORDS) != 0)
            return case_fail(__FILE__, __LINE__);
        phase_mark("software-golden");
        a = v2_expected(LINE_A);
        b = v2_expected(LINE_B);
        for (unsigned word = 0U; word < POLY_WORDS; ++word) {
            const uint32_t rhs = immediate_mode ? 255U : b[word];
            const uint32_t product =
                (uint32_t)(((uint64_t)a[word] * rhs) % MOD_Q0);
            const uint32_t accumulator = alias_source ? a[word] : initial;
            golden[word] = (uint32_t)(((uint64_t)accumulator +
                (uint64_t)repeats * product) % MOD_Q0);
        }
        if (v2_allow_output(LINE_OUT, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        /* 配置窗口逐寄存器写入、读回；COMMIT 才使这一组 BASE/SIZE 生效。 */
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
        if (expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U),
                       UINT32_MAX) != 0)
            return case_fail(__FILE__, __LINE__);
        if (expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0)
            return case_fail(__FILE__, __LINE__);
        if (expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
            return case_fail(__FILE__, __LINE__);
        csr_write(CSR_COMMIT, COMMIT);
        if (wait_window(1) != 0 || check_status() != 0)
            return case_fail(__FILE__, __LINE__);

        printf("[HPU][PMAC][ISSUE] mod -> A/B/accumulator DLOAD -> "
               "PMAC -> DSTORE line=%u count=%u -> PFREE inputs -> PSYNC\n",
               LINE_OUT, POLY_LINES);
        phase_mark("issue");
        if (dload_mod(LINE_MOD, 1U) != 0 || pmodld(0U) != 0)
            return case_fail(__FILE__, __LINE__);
        if (dload(P0, LINE_A, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        if (!alias_source && dload(P2, LINE_SCRATCH, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        if (!immediate_mode && dload(P1, LINE_B, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        for (unsigned issued = 0U; issued < repeats; ++issued) {
            rc = immediate_mode ? op_mac_imm(dst, P0, 255U) :
                op_mac(dst, P0, P1);
            if (rc != 0) {
                printf("[HPU][PMAC][FAIL] phase=issue index=%u rc=%d\n",
                       issued, rc);
                return case_fail(__FILE__, __LINE__);
            }
        }
        if (dstore_release(dst, LINE_OUT, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        if ((!alias_source && pfree(P0) != 0) || (!immediate_mode && pfree(P1) != 0) ||
            pfree(P4) != 0)
            return case_fail(__FILE__, __LINE__);
        /* 整段程序只在末尾 PSYNC；等待完成且空闲，然后消费本轮完成电平。 */
        psync();
        phase_mark("wait-completion");
        rc = wait_irq();
        if (rc != 0) {
            printf("[HPU][FAIL] phase=terminal-psync rc=%d\n", rc);
            return case_fail(__FILE__, __LINE__);
        }
        rc = completion_clear();
        if (rc != 0) {
            printf("[HPU][FAIL] phase=clear-completion rc=%d\n", rc);
            return case_fail(__FILE__, __LINE__);
        }
        if (check_status() != 0)
            return case_fail(__FILE__, __LINE__);
        phase_mark("compare-results");
        const int data_rc = v2_check_words("PMAC", LINE_OUT, golden, POLY_WORDS, MOD_Q0);
        phase_mark("check-guard");
        const int guard_rc = v2_check_memory("readonly-and-guard");
        if (data_rc != 0 || guard_rc != 0)
            return case_fail(__FILE__, __LINE__);
        phase_mark("round-done");
        printf("[HPU][PMAC][ROUND-PASS] variant=%u compared=%u "
               "readonly-and-guard=pass\n", variant, POLY_WORDS);
    }
    return case_pass(__FILE__);
}
