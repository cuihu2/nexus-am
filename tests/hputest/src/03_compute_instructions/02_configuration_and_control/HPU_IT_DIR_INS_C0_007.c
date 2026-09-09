#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>

/*
 * 测试点：IT-INS-C0-007
 * 目的：同一程序中选择 q0 -> q1 -> q0，逐次写回模乘结果。
 * 同一组 A/B 始终驻留，p2 每次 DSTORE 后释放并重用；最后才发一次 PSYNC。
 * 额外结果区 line 384..447 位于本用例 512-line 窗口内，不改变冒烟 DDR 布局。
 */
int main(void) {
    static uint32_t golden[POLY_WORDS];
    const unsigned outputs[3] = {LINE_OUT, LINE_OUT_B, 384U};
    const unsigned contexts[3] = {0U, 6U, 0U};
    const uint32_t moduli[3] = {MOD_Q0, MOD_Q1, MOD_Q0};
    case_start(__FILE__);

    for (unsigned profile = 0U; profile < 2U; ++profile) {
        const uint32_t *a;
        const uint32_t *b;
        unsigned distinguishing = 0U;
        int rc;

        printf("[HPU][PMODLD][ROUND] profile=%u context=0/6/0 "
               "q=%u/%u/%u words=%u\n",
               profile, MOD_Q0, MOD_Q1, MOD_Q0, POLY_WORDS);
        if (v2_prepare(profile, MOD_Q0, MOD_Q1) != 0)
            return case_fail(__FILE__, __LINE__);
        a = v2_expected(LINE_A);
        b = v2_expected(LINE_B);
        for (unsigned word = 0U; word < POLY_WORDS; ++word) {
            const uint64_t product = (uint64_t)a[word] * b[word];
            if (product % MOD_Q0 != product % MOD_Q1) ++distinguishing;
        }
        if (distinguishing == 0U) {
            printf("[HPU][PMODLD][FAIL] phase=fixture reason=no-distinguishing-result\n");
            return case_fail(__FILE__, __LINE__);
        }
        printf("[HPU][PMODLD][DATA] distinguishing-coefficients=%u "
               "output-lines=%u/%u/%u\n", distinguishing,
               outputs[0], outputs[1], outputs[2]);
        for (unsigned step = 0U; step < 3U; ++step) {
            if (v2_allow_output(outputs[step], POLY_LINES) != 0)
                return case_fail(__FILE__, __LINE__);
        }
        /* 配置窗口逐寄存器写入、读回；COMMIT 才使这一组 BASE/SIZE 生效。 */
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

        printf("[HPU][PMODLD][ISSUE] mod/A/B DLOAD -> "
               "(PMODLD -> PMUL -> DSTORE) x3 -> PFREE inputs -> PSYNC\n");
        if (dload_mod(LINE_MOD, 1U) != 0 ||
            dload(P0, LINE_A, POLY_LINES) != 0 ||
            dload(P1, LINE_B, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        for (unsigned step = 0U; step < 3U; ++step) {
            if (pmodld(contexts[step]) != 0 ||
                op_mul(P2, P0, P1) != 0 ||
                dstore_release(P2, outputs[step], POLY_LINES) != 0) {
                printf("[HPU][PMODLD][FAIL] phase=issue step=%u "
                       "context=%u output-line=%u\n",
                       step, contexts[step], outputs[step]);
                return case_fail(__FILE__, __LINE__);
            }
        }
        if (pfree(P0) != 0 || pfree(P1) != 0 || pfree(P4) != 0)
            return case_fail(__FILE__, __LINE__);
        /* 整段程序只在末尾 PSYNC；等待完成且空闲，然后消费本轮完成电平。 */
        psync();
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
        if (check_status() != 0 || v2_check_memory("readonly-and-guard") != 0)
            return case_fail(__FILE__, __LINE__);

        for (unsigned step = 0U; step < 3U; ++step) {
            for (unsigned word = 0U; word < POLY_WORDS; ++word)
                golden[word] = (uint32_t)(((uint64_t)a[word] * b[word]) %
                                         moduli[step]);
            printf("[HPU][PMODLD][CHECK] step=%u context=%u q=%u "
                   "output-line=%u\n",
                   step, contexts[step], moduli[step], outputs[step]);
            if (v2_check_words("PMODLD-result", outputs[step], golden,
                               POLY_WORDS, moduli[step]) != 0)
                return case_fail(__FILE__, __LINE__);
        }
        printf("[HPU][PMODLD][ROUND-PASS] profile=%u transitions=0-6-0 "
               "readonly-and-guard=pass\n", profile);
    }
    return case_pass(__FILE__);
}
