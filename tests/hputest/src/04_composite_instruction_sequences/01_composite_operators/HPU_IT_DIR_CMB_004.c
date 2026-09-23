#include <hpu/log.h>
#include <hpu/completion.h>
#include <hpu/keyswitch_case.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <keyswitch_delivery.h>

_Static_assert(KEYSWITCH_ACTIVE_LINES == HPU_KEYSWITCH_WINDOW_LINES &&
               KEYSWITCH_GUARD_OFFSET == HPU_KEYSWITCH_GUARD_OFFSET &&
               KEYSWITCH_GUARD_LINES == HPU_KEYSWITCH_GUARD_LINES &&
               KEYSWITCH_TOTAL_LINES == HPU_KEYSWITCH_TOTAL_LINES &&
               KEYSWITCH_SCRATCH_OFFSET == HPU_KEYSWITCH_SCRATCH_OFFSET &&
               KEYSWITCH_OUTPUT_OFFSET == HPU_KEYSWITCH_OUTPUT_OFFSET,
               "KeySwitch runtime layout differs from validated delivery");

/*
 * 测试点：IT-CMB-004
 * 目的：KeySwitch 全链。
 * 固定N4096/Q4/P3/D2；独立KeySwitch包提供输入、relinearization key和golden，
 * 同批Auto包仅提供缺失的ModUp/ModDown常量见证。716条DMA由AM importer独立绑定。
 * 逐个component/basis比较输出，并检查输入、密钥、常量和尾部guard没有被改写。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("prepare");
    if (keyswitch_prepare() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("configure");
    /* 清除上一轮FAULT/IRQ，并让本例的完成通知只能来自末尾唯一PSYNC。 */
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, KEYSWITCH_TOTAL_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, KEYSWITCH_TOTAL_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    LOG_DEBUG("[HPU][KEYSWITCH][ISSUE] instructions=2167 dma=%u "
           "terminal_psync=1\n", HPU_KEYSWITCH_DMA_COUNT);
    phase_mark("issue");
    rc = hpu_program_keyswitch(keyswitch_spans, HPU_KEYSWITCH_DMA_COUNT);
    if (rc != 0) {
        LOG_ERROR("[HPU][KEYSWITCH][FAIL] phase=producer-program rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }

    phase_mark("wait-completion");
    rc = wait_irq();
    if (rc != 0) {
        LOG_ERROR("[HPU][KEYSWITCH][FAIL] phase=terminal-psync rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    if (completion_clear() != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    phase_mark("compare-results");
    const int data_rc = keyswitch_check_results();
    phase_mark("check-guard");
    const int guard_rc = keyswitch_check_memory();
    if (data_rc != 0 || guard_rc != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
