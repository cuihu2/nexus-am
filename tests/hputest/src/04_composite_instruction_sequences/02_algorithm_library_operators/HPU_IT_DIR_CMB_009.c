#include <hpu/completion.h>
#include <hpu/hadd_case.h>
#include <hpu/log.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <hadd_delivery.h>

_Static_assert(HPU_PROGRAM_HADD_DMA_COUNT == HPU_HADD_DMA_COUNT,
               "HADD runtime and validated relocation counts differ");

/*
 * 测试点：IT-CMB-009
 * 目的：通过固定HPU_SEAL BFV算法库接口验证HADD。
 * 固定N4096/Q4、2-component、coefficient-domain密文，由
 * BfvOperationPlan::append_add生成完整程序和25条resolved DMA。逐component/basis
 * 对照SEAL Evaluator::add golden，并检查两份输入、模数表和尾部guard不被改写。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("prepare");
    if (hadd_prepare() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("configure");
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, HPU_HADD_TOTAL_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, HPU_HADD_TOTAL_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    LOG_DEBUG("[HPU][HADD][ISSUE] api=hpu::seal_adapter::BfvOperationPlan::append_add "
              "instructions=%u dma=%u terminal_psync=1\n",
              HPU_HADD_INSTRUCTION_COUNT, HPU_HADD_DMA_COUNT);
    phase_mark("issue");
    rc = hpu_run_hadd();
    if (rc != 0) {
        LOG_ERROR("[HPU][HADD][FAIL] phase=producer-program rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }

    phase_mark("wait-completion");
    rc = wait_irq();
    if (rc != 0) {
        LOG_ERROR("[HPU][HADD][FAIL] phase=terminal-psync rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    if (completion_clear() != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("compare-results");
    const int data_rc = hadd_check_results();
    phase_mark("check-guard");
    const int guard_rc = hadd_check_memory();
    if (data_rc != 0 || guard_rc != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
