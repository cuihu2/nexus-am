#include <hpu/application_case.h>
#include <hpu/completion.h>
#include <hpu/log.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <bgv_multiply_chain.h>

_Static_assert((unsigned)HPU_PROGRAM_BGV_MULTIPLY_CHAIN_DMA_COUNT ==
                   (unsigned)HPU_APPLICATION_DMA_COUNT,
               "APP008 DMA ABI drift");

/*
 * 测试点：IT-APP-008
 * 目的：执行 BGV AddPlain -> Multiply+Relinearize -> ModSwitch -> AddPlain 应用链。
 * 生成阶段要求 SEAL 真值与 BgvSoftwareExecutor 的20个物理 RNS limb 逐字一致；
 * 目标机执行1124条固定指令并检查每一步输出、只读输入/密钥和64-line尾部guard。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("prepare");
    if (application_prepare() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("configure");
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, HPU_APPLICATION_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, HPU_APPLICATION_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("issue");
    rc = hpu_run_bgv_multiply_chain();
    if (rc != 0) {
        LOG_ERROR("[HPU][APP008][FAIL] phase=producer-program rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    phase_mark("wait-completion");
    if (wait_irq() != 0 || completion_clear() != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("compare-results");
    const int data_rc = application_check_results();
    phase_mark("check-guard");
    const int guard_rc = application_check_memory();
    if (data_rc != 0 || guard_rc != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
