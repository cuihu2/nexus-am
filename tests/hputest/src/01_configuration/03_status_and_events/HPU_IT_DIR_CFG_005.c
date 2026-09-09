#include <hpu/result.h>
#include <hpu/it_v2.h>
#include <hpu/completion.h>

/*
 * 测试点：IT-CFG-005
 * 目的：FAULT方向对象记录与W1C可观察。
 * 模式：定向故障可观察（P2）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_FAULT_INJECTION
 *   - HPU_REQ_CACHE_CONTRACT
 */

#define FAULT_IS_LOAD (1U << 1)
#define FAULT_OBJECT_MASK (7U << 4)

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
    const uint32_t seed = 0u;
    printf("[HPU][DATA] profile=producer seed_tag=0x%x A_line=%u B_line=%u "
           "words=%u q=%u window_base=0x%lx window_lines=%u\n",
           seed, LINE_A, LINE_B, POLY_WORDS, MOD_Q0,
           (unsigned long)MEM_BASE, WINDOW_LINES);
    const uint32_t expected_fault = FAULT_VALID | FAULT_IS_LOAD;
    uint32_t fault = 0U;
    uint32_t status = 0U;
    unsigned timeout;

    if (v2_prepare(0U, MOD_Q0, MOD_Q1) != 0) return failure(__LINE__, phase);

    phase = "clear-old-events";
    printf("[HPU][CLEAR] FAULT.W1C=0x%x IRQ.W1C=0x%x\n", FAULT_VALID, IRQ_LEVEL);
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    if (expect_csr(CSR_FAULT, 0U, FAULT_VALID) != 0)
        return failure(__LINE__, phase);

    phase = "write-shadow";
    printf("[HPU][CONFIG] expected_base=0x%lx expected_lines=%u\n",
           (unsigned long)MEM_BASE, WINDOW_LINES);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, WINDOW_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_C(0xff)) != 0 ||
        expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, 1U) != 0)
        return failure(__LINE__, phase);
    phase = "commit-window";
    printf("[HPU][COMMIT] base_hi=0x%x base_lo=0x%x size_hi=0x%x size_lo=0x%x\n",
           csr_read(CSR_BASE_HI), csr_read(CSR_BASE_LO),
           csr_read(CSR_SIZE_HI), csr_read(CSR_SIZE_LO));
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0) return failure(__LINE__, phase);
    /* 注入前必须是有效、空闲且无故障，不能把初始化遗留 fault 当作本例成功。 */
    if (check_status() != 0) return failure(__LINE__, phase);

    /* x10=512 已越过合法 [0,512) window；producer DLOAD p0 应记录 load/p0 fault。 */
    phase = "expected-out-of-window-fault";
    printf("[HPU][NEGATIVE] DLOAD p0 line=%u count=1 window=[0,%u) "
           "expected_fault=0x%x mask=0x%x\n", WINDOW_LINES, WINDOW_LINES,
           expected_fault, FAULT_VALID | FAULT_IS_LOAD | FAULT_OBJECT_MASK);
    raw_dload_p0(WINDOW_LINES, 1U);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        fault = csr_read(CSR_FAULT);
        if ((fault & FAULT_VALID) != 0U) break;
    }
    printf("[HPU][FAULT-SAMPLE] fault=0x%x expected=0x%x polls=%u limit=%u\n",
           fault, expected_fault, timeout, TIMEOUT);
    if (timeout == TIMEOUT) return failure(__LINE__, phase);
    if ((fault & (FAULT_VALID | FAULT_IS_LOAD | FAULT_OBJECT_MASK)) !=
        expected_fault)
        return failure(__LINE__, phase);

    /* FAULT_STATUS[0] 为 W1C；日志仅辅助定位，必须读回确认清除。 */
    phase = "fault-W1C";
    printf("[HPU][FAULT-CLEAR] write=0x%x expected_valid=0\n", FAULT_VALID);
    csr_write(CSR_FAULT, FAULT_VALID);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        /* 两个地址分别采样；清除回传暂未一致时继续等，不能立即误报失败。 */
        fault = csr_read(CSR_FAULT);
        status = csr_read(CSR_STATUS);
        if ((fault & FAULT_VALID) == 0U &&
            (status & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) == STATUS_VALID)
            break;
    }
    printf("[HPU][FAULT-CLEAR-SAMPLE] fault=0x%x status=0x%x polls=%u limit=%u\n",
           fault, status, timeout, TIMEOUT);
    if (timeout == TIMEOUT) return failure(__LINE__, phase);
    if (check_status() != 0) return failure(__LINE__, phase);

    /* 外部 fault-injection 入口和波形覆盖仍由 IT/VCS 环境另行判定。 */
    phase = "readonly-and-guard";
    if (v2_check_memory(phase) != 0) return failure(__LINE__, phase);
    printf("[HPU][SW-CHECK-PASS] readonly-and-guard=pass window_lines=%u "
           "monitor=needs-monitor; not AXI/handshake coverage\n", WINDOW_LINES);
    return case_pass(__FILE__);
}
