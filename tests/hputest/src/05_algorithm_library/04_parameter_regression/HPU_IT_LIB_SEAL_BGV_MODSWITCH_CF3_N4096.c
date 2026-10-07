#include <hpu/application_case.h>
#include <hpu/completion.h>
#include <hpu/log.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <hpu/trace.h>
#include <bgv_modswitch_n4096.h>

/*
 * 测试点：HPU_IT_LIB_SEAL_BGV_MODSWITCH_CF3_N4096（原编号HPU_IT_DIR_CMB_013_BGV_MODSWITCH_N4096）
 * 目的：BGV降一层，输入correction factor为3；验证模t修正和输出golden。
 * 层级：05算法库参数回归，主机参考为SEAL；不宣称调用Poseidon API。
 * 保留原输入、指令、golden及guard检查，唯一PSYNC仍在完整程序末尾。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0) return case_fail(__FILE__, __LINE__);
    trace_phase(TRACE_PREPARE);
    phase_mark("prepare");
    if (application_prepare() != 0) return case_fail(__FILE__, __LINE__);

    trace_phase(TRACE_CONFIGURE);
    phase_mark("configure");
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, HPU_APPLICATION_CAPACITY_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, HPU_APPLICATION_CAPACITY_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0) return case_fail(__FILE__, __LINE__);

    trace_phase(TRACE_ISSUE);
    phase_mark("issue");
    rc = hpu_run_bgv_modswitch_n4096();
    if (rc != 0) {
        LOG_ERROR("[HPU][APP][PROGRAM-FAIL] rc=%d instruction=%u dma=%u\n",
                  rc, application_trace.instruction, application_trace.dma);
        return case_fail(__FILE__, __LINE__);
    }
    trace_phase(TRACE_WAIT);
    phase_mark("wait-completion");
    if (application_wait() != 0 || completion_clear() != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    trace_phase(TRACE_COMPARE);
    phase_mark("compare-results");
    const int data_rc = application_check_results();
    trace_phase(TRACE_GUARD);
    phase_mark("check-guard");
    const int guard_rc = application_check_memory();
    if (data_rc != 0 || guard_rc != 0) return case_fail(__FILE__, __LINE__);
    trace_phase(TRACE_DONE);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
