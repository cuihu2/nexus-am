#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/it_v2.h>
#include <hpu/completion.h>

/*
 * 测试点：IT-CFG-001
 * 目的：HPU相关CSR全量访问、修改与读回。
 * 模式：定向配置（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

/* 失败后补充只读 CSR 诊断；各寄存器独立采样，不是原子快照，不读 PLIC claim。 */
static int failure(unsigned source_line, const char *phase) {
    const uint32_t status = csr_read(CSR_STATUS);
    const uint32_t fault = csr_read(CSR_FAULT);
    const uint32_t irq = csr_read(CSR_IRQ);
    const uint32_t base_lo = csr_read(CSR_BASE_LO);
    const uint32_t base_hi = csr_read(CSR_BASE_HI);
    const uint32_t size_lo = csr_read(CSR_SIZE_LO);
    const uint32_t size_hi = csr_read(CSR_SIZE_HI);

    printf("[HPU][FAIL] phase=%s source_line=%u status=0x%x fault=0x%x irq=0x%x\n",
           phase, source_line, status, fault, irq);
    printf("[HPU][FAIL][window-shadow] base_hi=0x%x base_lo=0x%x "
           "size_hi=0x%x size_lo=0x%x\n", base_hi, base_lo, size_hi, size_lo);
    return case_fail(__FILE__, source_line);
}

int main(void) {
    const char *phase = "data-prepare";
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    const uint32_t seed = 0u;
    printf("[HPU][DATA] profile=producer seed_tag=0x%x A_line=%u B_line=%u "
           "words=%u q=%u window_base=0x%lx window_lines=%u\n",
           seed, LINE_A, LINE_B, POLY_WORDS, MOD_Q0,
           (unsigned long)MEM_BASE, WINDOW_LINES);
    uint32_t status_mask = STATUS_VALID | STATUS_BUSY |
        STATUS_FAULT;

    /* 只准备两组 4096×32-bit producer 数据；本函数不访问 HPU CSR。 */
    if (v2_prepare(0U, MOD_Q0, MOD_Q1) != 0) return failure(__LINE__, phase);

    /* 逐地址清除旧 fault/IRQ，确保本次读回不继承上一个用例的状态。 */
    phase = "clear-old-events";
    printf("[HPU][CLEAR] FAULT.W1C=0x%x IRQ.W1C=0x%x\n", FAULT_VALID, IRQ_LEVEL);
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    if (expect_csr(CSR_FAULT, 0U, FAULT_VALID) != 0)
        return failure(__LINE__, phase);
    if (expect_csr(CSR_IRQ, 0U, IRQ_LEVEL) != 0)
        return failure(__LINE__, phase);

    /* 四个 shadow CSR 均在 main 中按绝对地址写入并读回。 */
    phase = "write-shadow";
    printf("[HPU][CONFIG] expected_base=0x%lx expected_lines=%u\n",
           (unsigned long)MEM_BASE, WINDOW_LINES);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, WINDOW_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0)
        return failure(__LINE__, phase);
    if (expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_C(0xff)) != 0)
        return failure(__LINE__, phase);
    if (expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0)
        return failure(__LINE__, phase);
    if (expect_csr(CSR_SIZE_HI, 0U, 1U) != 0)
        return failure(__LINE__, phase);

    /* COMMIT 是写脉冲；STATUS.window-valid 才是配置生效的判据。 */
    phase = "commit-window";
    printf("[HPU][COMMIT] base_hi=0x%x base_lo=0x%x size_hi=0x%x size_lo=0x%x\n",
           csr_read(CSR_BASE_HI), csr_read(CSR_BASE_LO),
           csr_read(CSR_SIZE_HI), csr_read(CSR_SIZE_LO));
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0) return failure(__LINE__, phase);
    if (expect_csr(CSR_STATUS, STATUS_VALID, status_mask) != 0)
        return failure(__LINE__, phase);

    /* PSYNC 产生可读 IRQ；显式 W1C 后再次读回，覆盖最后一个 CSR。 */
    phase = "terminal-psync";
    psync();
    if (wait_irq() != 0) return failure(__LINE__, phase);
    if (check_status() != 0) return failure(__LINE__, phase);
    phase = "clear-completion";
    if (completion_clear() != 0) return failure(__LINE__, phase);
    if (expect_csr(CSR_IRQ, 0U, IRQ_LEVEL) != 0)
        return failure(__LINE__, phase);

    /* return 0 仅表示上述软件可见 CSR 自检通过；波形覆盖由 IT monitor 判定。 */
    phase = "readonly-and-guard";
    if (v2_check_memory(phase) != 0) return failure(__LINE__, phase);
    printf("[HPU][SW-CHECK-PASS] readonly-and-guard=pass window_lines=%u "
           "monitor=needs-monitor; not AXI/handshake coverage\n", WINDOW_LINES);
    return case_pass(__FILE__);
}
