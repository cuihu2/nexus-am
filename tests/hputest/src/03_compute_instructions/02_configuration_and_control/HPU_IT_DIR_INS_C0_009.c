#include <hpu/completion.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/progress.h>

/*
 * 测试点：IT-INS-C0-009
 * 目的：合法释放后复用同一逻辑对象号，检查内容已换成 B。
 * p0 基础输入、p7 边界输入；两轮均先 A -> PFREE -> B -> DSTORE。
 * 不测试手册未保证的重复释放/释放后非法访问，不对已由 DSTORE 释放的对象再 PFREE。
 */
int main(void) {
    case_start(__FILE__);
    if (progress_begin(__FILE__, 2U) != 0)
        return case_fail(__FILE__, __LINE__);

    for (unsigned profile = 0U; profile < 2U; ++profile) {
        if (!subcase_selected(profile)) continue;
        (void)result_context(__FILE__, profile);
        phase_mark("prepare");
        const unsigned object = profile == 0U ? P0 : P7;
        const uint32_t *expected;
        int rc;

        printf("[HPU][PFREE][ROUND] profile=%u object=p%u "
               "A-line=%u B-line=%u output-line=%u count=%u\n",
               profile, object, LINE_A, LINE_B, LINE_OUT, POLY_LINES);
        if (v2_prepare(profile, MOD_Q0, MOD_Q1) != 0)
            return case_fail(__FILE__, __LINE__);
        phase_mark("software-golden");
        expected = v2_expected(LINE_B);
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

        printf("[HPU][PFREE][ISSUE] DLOAD A -> PFREE p%u -> "
               "DLOAD B same-object -> DSTORE release -> PSYNC\n", object);
        phase_mark("issue");
        if (dload(object, LINE_A, POLY_LINES) != 0 ||
            pfree(object) != 0 ||
            dload(object, LINE_B, POLY_LINES) != 0 ||
            dstore_release(object, LINE_OUT, POLY_LINES) != 0)
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
        const int data_rc = v2_check_words("PFREE-reuse-B", LINE_OUT, expected,
                           POLY_WORDS, MOD_Q0);
        phase_mark("check-guard");
        const int guard_rc = v2_check_memory("readonly-and-guard");
        if (data_rc != 0 || guard_rc != 0)
            return case_fail(__FILE__, __LINE__);
        phase_mark("round-done");
        printf("[HPU][PFREE][ROUND-PASS] object=p%u compared=%u "
               "readonly-and-guard=pass\n", object, POLY_WORDS);
    }
    return case_pass(__FILE__);
}
