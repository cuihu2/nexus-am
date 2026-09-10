#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/progress.h>

/*
 * 测试点：IT-INS-C0-003
 * 目的：PMUL 对象模式、立即数模式和模回绕。
 * 六轮依次为基础对象、基础立即数 7、边界原地对象、边界立即数 0/1/255。
 * 立即数用 unsigned 传入编码器生成的适配接口，不把常量截成有符号 8 位。
 */
int main(void) {
    static uint32_t golden[POLY_WORDS];
    case_start(__FILE__);
    if (progress_begin(__FILE__, 6U) != 0)
        return case_fail(__FILE__, __LINE__);

    for (unsigned variant = 0U; variant < 6U; ++variant) {
        if (!subcase_selected(variant)) continue;
        (void)result_context(__FILE__, variant);
        phase_mark("prepare");
        const unsigned profile = variant < 2U ? 0U : 1U;
        const unsigned object_mode = variant == 0U || variant == 2U;
        const unsigned dst = variant == 2U ? P0 : P2;
        const unsigned immediate = variant == 1U ? 7U :
            variant == 4U ? 1U : variant == 5U ? 255U : 0U;
        const uint32_t *a;
        const uint32_t *b;
        int rc;

        printf("[HPU][PMUL][ROUND] variant=%u profile=%u dst=p%u "
               "mode=%s immediate=%u words=%u q=%u\n",
               variant, profile, dst, object_mode ? "object" : "immediate",
               immediate, POLY_WORDS, MOD_Q0);
        if (v2_prepare(profile, MOD_Q0, MOD_Q1) != 0)
            return case_fail(__FILE__, __LINE__);
        phase_mark("software-golden");
        a = v2_expected(LINE_A);
        b = v2_expected(LINE_B);
        for (unsigned word = 0U; word < POLY_WORDS; ++word) {
            const uint32_t rhs = object_mode ? b[word] : immediate;
            golden[word] = (uint32_t)(((uint64_t)a[word] * rhs) % MOD_Q0);
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

        printf("[HPU][PMUL][ISSUE] mod -> input DLOAD -> PMUL -> "
               "DSTORE line=%u count=%u -> PFREE inputs -> PSYNC\n",
               LINE_OUT, POLY_LINES);
        phase_mark("issue");
        if (dload_mod(LINE_MOD, 1U) != 0 || pmodld(0U) != 0)
            return case_fail(__FILE__, __LINE__);
        if (dload(P0, LINE_A, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        if (object_mode) {
            if (dload(P1, LINE_B, POLY_LINES) != 0 ||
                op_mul(dst, P0, P1) != 0)
                return case_fail(__FILE__, __LINE__);
        } else if (op_mul_imm(dst, P0, immediate) != 0) {
            return case_fail(__FILE__, __LINE__);
        }
        if (dstore_release(dst, LINE_OUT, POLY_LINES) != 0)
            return case_fail(__FILE__, __LINE__);
        /* 未装载 p1 的立即数轮次，不对 p1 发送 PFREE。 */
        if ((dst != P0 && pfree(P0) != 0) ||
            (object_mode && pfree(P1) != 0) || pfree(P4) != 0)
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
        const int data_rc = v2_check_words("PMUL", LINE_OUT, golden, POLY_WORDS, MOD_Q0);
        phase_mark("check-guard");
        const int guard_rc = v2_check_memory("readonly-and-guard");
        if (data_rc != 0 || guard_rc != 0)
            return case_fail(__FILE__, __LINE__);
        phase_mark("round-done");
        printf("[HPU][PMUL][ROUND-PASS] variant=%u compared=%u "
               "readonly-and-guard=pass\n", variant, POLY_WORDS);
    }
    return case_pass(__FILE__);
}
