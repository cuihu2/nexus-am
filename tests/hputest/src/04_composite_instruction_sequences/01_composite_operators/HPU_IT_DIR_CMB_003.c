#include <hpu/completion.h>
#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/progress.h>
#include <hpu/transform.h>
#include <intt/delivery.h>

/*
 * 测试点：IT-CMB-003
 * 目的：完整 negacyclic INTT，N=4096、Q0 基础数据。
 * 输入是 producer 自然顺序 NTT 域数据，不依赖002先运行。
 * 12级逆 twiddle、最后 N^-1*psi^-i 和独立系数域 golden 来自同批交付。
 * 本例不声称已经覆盖不同模数基、全规模和边界数据。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("prepare");
    printf("[HPU][INTT][CONFIG] N=%u q=%u stages=12 data=p0 twiddle=p1 mod=p2 "
           "input=natural-NTT output=natural-coefficient\n", TRANSFORM_N, TRANSFORM_Q);
    if (transform_prepare(transform_intt_image) != 0)
        return case_fail(__FILE__, __LINE__);
    transform_print_bindings(transform_intt_bindings, HPU_PROGRAM_INTT_DMA_COUNT);

    /* 所有输入、常量先写DDR，再按BASE/SIZE/COMMIT提交窗口。 */
    phase_mark("configure");
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, TRANSFORM_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(CSR_SIZE_LO, TRANSFORM_LINES, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    printf("[HPU][INTT][ISSUE] mod/input -> PINTT stage0..11(each DLOAD/PFREE twiddle) "
           "-> PMUL post-untwist-scale -> DSTORE -> PFREE mod -> PSYNC\n");
    /* 16次DMA和全部指令保持上游顺序，只有producer末尾的一条PSYNC。 */
    phase_mark("issue");
    rc = hpu_program_intt(transform_intt_spans, HPU_PROGRAM_INTT_DMA_COUNT);
    if (rc != 0) {
        printf("[HPU][INTT][FAIL] phase=producer-program rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    printf("[HPU][INTT][WAIT] terminal-psync issued; wait IRQ and not-busy\n");
    phase_mark("wait-completion");
    rc = wait_irq();
    if (rc != 0) {
        printf("[HPU][INTT][FAIL] phase=terminal-psync rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    rc = completion_clear();
    if (rc != 0) {
        printf("[HPU][INTT][FAIL] phase=clear-completion rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    if (check_status() != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("compare-results");
    const int data_rc = transform_check_result(transform_intt_golden);
    phase_mark("check-guard");
    const int guard_rc = transform_check_memory(transform_intt_image);
    if (data_rc != 0 || guard_rc != 0) return case_fail(__FILE__, __LINE__);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
