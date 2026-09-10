#include <hpu/bconv_case.h>
#include <hpu/completion.h>
#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/progress.h>
#include <bconv_delivery.h>

/*
 * 测试点：IT-CMB-001
 * 目的：producer 完整 Q4→P3 FastBConv，N=4096 基础数据。
 * 先逐 Q 计算 normalized，再逐 P 用 PMUL/PMAC 累加；只在程序末尾 PSYNC。
 * 最终输出与 producer 独立 FastBConv golden 比较，不使用精确 CRT 还原替代。
 * P→Q、边界数据、其他规模仍未覆盖，不能把本例 PASS 当作整个测试点全覆盖。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("prepare");
    printf("[HPU][BCONV][SCOPE] Q4->P3 N4096 basic; "
           "P->Q/boundary/other-sizes NOT_COVERED\n");
    if (bconv_prepare() != 0)
        return case_fail(__FILE__, __LINE__);
    for (unsigned basis = 0U; basis < BCONV_Q_COUNT + BCONV_P_COUNT; ++basis)
        printf("[HPU][BCONV][MOD] context=%u modulus=%u\n", basis, bconv_moduli[basis]);
    for (unsigned dma = 0U; dma < HPU_PROGRAM_BCONV_DMA_COUNT; ++dma)
        printf("[HPU][BCONV][DMA-PLAN] dma=%u op=%s line=%u count=%u\n", dma,
               bconv_dma_objects[dma], bconv_spans[dma].line_offset, bconv_spans[dma].line_count);

    /* BASE/SIZE/COMMIT 逐寄存器可见；2048line容纳Q4/P3常量、scratch和尾部guard。 */
    phase_mark("configure");
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, BCONV_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(CSR_SIZE_LO, BCONV_LINES, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    printf("[HPU][BCONV][ISSUE] load mod p4 -> Q0..3 normalized PMUL p0 "
           "-> P0..2 PMUL/PMAC p2 -> DSTORE outputs -> PFREE mod -> terminal PSYNC\n");
    phase_mark("issue");
    rc = hpu_program_bconv(bconv_spans, HPU_PROGRAM_BCONV_DMA_COUNT);
    if (rc != 0) {
        printf("[HPU][BCONV][FAIL] phase=producer-program rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    /* producer 已发出唯一末尾PSYNC；MMIO等待只观察完成，不额外发令。 */
    phase_mark("wait-completion");
    rc = wait_irq();
    if (rc != 0) {
        printf("[HPU][BCONV][FAIL] phase=terminal-psync rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    rc = completion_clear();
    if (rc != 0) {
        printf("[HPU][BCONV][FAIL] phase=clear-completion rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    if (check_status() != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("compare-results");
    const int data_rc = bconv_check_results(bconv_moduli);
    phase_mark("check-guard");
    const int guard_rc = bconv_check_memory();
    if (data_rc != 0 || guard_rc != 0) return case_fail(__FILE__, __LINE__);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
