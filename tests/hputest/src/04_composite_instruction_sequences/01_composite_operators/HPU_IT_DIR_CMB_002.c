#include <hpu/completion.h>
#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/progress.h>
#include <hpu/transform.h>
#include <ntt/delivery.h>

/*
 * 测试点：IT-CMB-002
 * 目的：完整 negacyclic NTT，N=4096、Q0 基础数据。
 * 输入/模表/pre-twist/12 级 twiddle/golden 均来自同批 producer。
 * main 可见 CSR 和同步；算法函数保留 producer 的完整指令顺序，16 次 DMA
 * 由构建时解析的 line_map 绑定。不得在每级加入额外 PSYNC。
 * 本例不声称已经覆盖不同模数基、全规模和边界数据。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("prepare");
    printf("[HPU][NTT][CONFIG] N=%u q=%u stages=12 data=p0 scratch=p3 twiddle=p1 mod=p2 "
           "input=bit-reversed-coefficient output=P-network-NTT\n", TRANSFORM_N, TRANSFORM_Q);
    if (transform_prepare(transform_ntt_image) != 0)
        return case_fail(__FILE__, __LINE__);
    transform_print_bindings(transform_ntt_bindings, HPU_PROGRAM_NTT_DMA_COUNT);

    /* 新窗口独立容纳全部 12 级表及 guard，不复用 00 的256line窗口。 */
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

    printf("[HPU][NTT][ISSUE] mod/input -> PMUL pre-twist -> "
           "PNTT stage0..11(p0/p3 ping-pong, PFREE src/twiddle) -> DSTORE -> PFREE mod -> PSYNC\n");
    /* 函数末尾已包含唯一 PSYNC；这里不再额外发送。 */
    phase_mark("issue");
    rc = hpu_program_ntt(transform_ntt_spans, HPU_PROGRAM_NTT_DMA_COUNT);
    if (rc != 0) {
        printf("[HPU][NTT][FAIL] phase=producer-program rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    printf("[HPU][NTT][WAIT] terminal-psync issued; wait IRQ and not-busy\n");
    phase_mark("wait-completion");
    rc = wait_irq();
    if (rc != 0) {
        printf("[HPU][NTT][FAIL] phase=terminal-psync rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    rc = completion_clear();
    if (rc != 0) {
        printf("[HPU][NTT][FAIL] phase=clear-completion rc=%d\n", rc);
        return case_fail(__FILE__, __LINE__);
    }
    if (check_status() != 0)
        return case_fail(__FILE__, __LINE__);
    phase_mark("compare-results");
    const int data_rc = transform_check_result(transform_ntt_golden);
    phase_mark("check-guard");
    const int guard_rc = transform_check_memory(transform_ntt_image);
    if (data_rc != 0 || guard_rc != 0) return case_fail(__FILE__, __LINE__);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
