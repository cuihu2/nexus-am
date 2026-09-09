#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>

/*
 * 测试点：IT-INS-C0-001
 * 目的：逐系数模加、模回绕。
 * 基础数据使用独立目的 p2；边界数据覆盖 p0/p1 原地写回以及 p2 的连续依赖。
 * 每轮独立准备数据，所有指令之后只发一次 PSYNC；最终检查全部系数和非输出 DDR。
 */
int main(void) {
    static uint32_t golden[POLY_WORDS];
    case_start(__FILE__);

    for (unsigned variant = 0U; variant < 4U; ++variant) {
        const unsigned profile = variant == 0U ? 0U : 1U;
        const unsigned dst = variant == 1U ? P0 : variant == 2U ? P1 : P2;
        const unsigned dependent = variant == 3U;
        const uint32_t *a;
        const uint32_t *b;
        int rc;

        printf("[HPU][PADD][ROUND] variant=%u profile=%u dst=p%u "
               "lhs=p0 rhs=p1 dependent=%u words=%u q=%u\n",
               variant, profile, dst, dependent, POLY_WORDS, MOD_Q0);
        if (v2_prepare(profile, MOD_Q0, MOD_Q1) != 0)
            return case_fail(__FILE__, __LINE__);
        a = v2_expected(LINE_A);
        b = v2_expected(LINE_B);
        for (unsigned word = 0U; word < POLY_WORDS; ++word) {
            uint64_t sum = (uint64_t)a[word] + b[word];
            uint32_t value = (uint32_t)(sum % MOD_Q0);
            if (dependent) value = (uint32_t)(((uint64_t)value + a[word]) % MOD_Q0);
            golden[word] = value;
        }
        if (v2_allow_output(LINE_OUT, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
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

        printf("[HPU][PADD][ISSUE] mod -> A/B -> PADD%s -> "
               "DSTORE line=%u count=%u -> PFREE inputs -> PSYNC\n",
               dependent ? " -> dependent PADD" : "", LINE_OUT, POLY_LINES);
        if (dload_mod(LINE_MOD, 1U) != 0 || pmodld(0U) != 0)
            return case_fail(__FILE__, __LINE__);
        if (dload(P0, LINE_A, POLY_LINES) != 0 ||
            dload(P1, LINE_B, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        if (op_add(dst, P0, P1) != 0)
            return case_fail(__FILE__, __LINE__);
        if (dependent && op_add(P2, P2, P0) != 0)
            return case_fail(__FILE__, __LINE__);
        if (dstore_release(dst, LINE_OUT, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        /* DSTORE 已释放目的对象；只能释放仍驻留的输入和模表对象。 */
        if ((dst != P0 && pfree(P0) != 0) ||
            (dst != P1 && pfree(P1) != 0) || pfree(P4) != 0)
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

        if (v2_check_words("PADD", LINE_OUT, golden, POLY_WORDS, MOD_Q0) != 0)
            return case_fail(__FILE__, __LINE__);
        printf("[HPU][PADD][ROUND-PASS] variant=%u compared=%u "
               "readonly-and-guard=pass\n", variant, POLY_WORDS);
    }
    return case_pass(__FILE__);
}
