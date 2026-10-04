#include <hpu/application_case.h>
#include <hpu/completion.h>
#include <hpu/log.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <hpu/trace.h>
#include <bgv_rotate_n4096.h>

/*
 * 测试点：IT-CMB-014
 * 目的：BGV，N=4096。验证左旋一格及 Galois KeySwitch；CKKS 槽旋转和 BFV/BGV 行旋转分别生成。
 * 指令、模表、密钥和初始数据来自 inline-asm main 同一批交付。
 * 目标直接比对 SEAL 物理 golden；布局/rounded-P 适配另有记录。
 * 每个 ELF 只执行一次，末尾唯一 PSYNC；silent 保留 application_trace。
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
    rc = hpu_run_bgv_rotate_n4096();
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
