#include <hpu/ckks_case.h>
#include <hpu/completion.h>
#include <hpu/log.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <ckks_rescale.h>

/*
 * 测试点：IT-CMB-013
 * 目的：通过固定 HPU_SEAL CKKS 算法库接口验证独立 Rescale。
 * 输入为 N4096/Q4|P1、canonical-NTT 的两分量密文，scale=2^50；
 * 程序只包含 CkksOperationPlan::append_rescale 生成的 Q4->Q3 舍入降层。
 * 输入、常量、scratch、DMA 重定位和精确 SEAL golden 均来自同一次
 * producer。输入转换区、scratch 和输出可写，常量、twiddle 与尾部 guard
 * 必须保持不变；所有写区仍由 producer DMA 清单逐 line 约束。
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

    LOG_DEBUG("[HPU][CKKS][RESCALE][ISSUE] "
              "api=hpu::seal_adapter::CkksOperationPlan::append_rescale "
              "N=%u Q=4->%u inputs=2 outputs=2\n", CKKS_N, CKKS_Q);
    phase_mark("issue");
    rc = hpu_run_ckks_rescale();
    if (rc != 0) {
        LOG_ERROR("[HPU][CKKS][RESCALE][FAIL] phase=producer-program rc=%d\n", rc);
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
