#include <hpu/ckks_case.h>
#include <hpu/completion.h>
#include <hpu/log.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <ckks_composed_application.h>

/*
 * 测试点：IT-APP-003
 * 目的：x*(RotateLeft(x,1)+Conjugate(x))+1：N=128，Q3|P1→Q2。
 * 完整程序只执行一遍，所有 DSTORE 后仅一次 PSYNC；不重复多组 profile。
 * 密钥/输入/golden 来自同次上游主机生成，SEAL 精确密文检查和解密误差检查
 * 在生成阶段完成；目标机只比最终密文原始字及只读/guard，不在 UART 全量打印。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("prepare");
    if (ckks_prepare() != 0)
        return case_fail(__FILE__, __LINE__);

    // 仅配置实际使用的窗口与尾部 guard，不把上游预留容量误当成数据量。
    phase_mark("configure");
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, CKKS_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, CKKS_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    // 原样执行 inline-asm main 生成的固定指令和 x10/x11 重定位。
    phase_mark("issue");
    rc = hpu_run_ckks_composed_application();
    if (rc != 0) {
        LOG_ERROR("[HPU][CKKS][FAIL] phase=producer-program rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    phase_mark("wait-completion");
    if (wait_irq() != 0 || completion_clear() != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("compare-results");
    const int data_rc = ckks_check_results();
    phase_mark("check-guard");
    const int guard_rc = ckks_check_memory();
    if (data_rc != 0 || guard_rc != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
