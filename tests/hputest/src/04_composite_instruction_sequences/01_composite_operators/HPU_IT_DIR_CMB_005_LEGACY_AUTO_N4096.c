#include <hpu/auto_case.h>
#include <hpu/completion.h>
#include <hpu/log.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <auto_delivery.h>

_Static_assert(HPU_PROGRAM_AUTO_DMA_COUNT == HPU_AUTO_DMA_COUNT,
               "Auto runtime and validated relocation counts differ");

/*
 * 测试点：IT-CMB-005
 * 目的：NTT 与 Auto 联合序列。
 * 固定N4096/Q4/P3/D2、galois element 3（generator-3左旋一步）。原始密文先在
 * HPU执行标准NTT和modified-root融合Auto INTT，再执行Galois KeySwitch。
 * 941条DMA全部采用producer resolved plan；逐component/basis比较最终密文，并检查
 * 输入、Galois key、常量和尾部guard不被改写。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("prepare");
    if (auto_prepare() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("configure");
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, HPU_AUTO_TOTAL_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, HPU_AUTO_TOTAL_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    LOG_DEBUG("[HPU][AUTO][ISSUE] instructions=3009 dma=%u galois=3 "
              "rotation_step=1 terminal_psync=1\n", HPU_AUTO_DMA_COUNT);
    phase_mark("issue");
    rc = hpu_program_auto(auto_spans, HPU_AUTO_DMA_COUNT);
    if (rc != 0) {
        LOG_ERROR("[HPU][AUTO][FAIL] phase=producer-program rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }

    phase_mark("wait-completion");
    rc = wait_irq();
    if (rc != 0) {
        LOG_ERROR("[HPU][AUTO][FAIL] phase=terminal-psync rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    if (completion_clear() != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("compare-results");
    const int data_rc = auto_check_results();
    phase_mark("check-guard");
    const int guard_rc = auto_check_memory();
    if (data_rc != 0 || guard_rc != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
