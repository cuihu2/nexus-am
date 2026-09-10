#include <hpu/result.h>
#include <hpu/report.h>
#include <hpu/it_v2.h>
#include <hpu/completion.h>

/*
 * 测试点：IT-PATH-001
 * 目的：cmd_kind=0（物理 opcode=0x5B）命令单次接收与反压保持。
 * 模式：定向通路（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
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
    int rc;

    /* data-only：ELF 中的两组 4096-word 输入和模数记录先落到 DDR。 */
    if (v2_prepare(0U, MOD_Q0, MOD_Q1) != 0) return failure(__LINE__, phase);
    phase = "clear-old-events";
    printf("[HPU][CLEAR] FAULT.W1C=0x%x IRQ.W1C=0x%x\n", FAULT_VALID, IRQ_LEVEL);
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    if (expect_csr(CSR_FAULT, 0U, FAULT_VALID) != 0 ||
        expect_csr(CSR_IRQ, 0U, IRQ_LEVEL) != 0)
        return failure(__LINE__, phase);

    /* HPU 初始化只包含 BASE、SIZE、COMMIT；四个 shadow CSR 均读回。 */
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
    if (check_status() != 0) return failure(__LINE__, phase);

    /* custom1 DLOAD 模数表后，cmd_kind=0（物理 opcode=0x5B） PMODLD(0) 是本例被观察命令。 */
    phase = "configuration-command";
    printf("[HPU][ISSUE] DLOAD mod p4 line=%u count=1 -> PMODLD context=0 "
           "-> PFREE p4 -> PSYNC; cmd_kind=0 physical_opcode=0x5b\n", LINE_MOD);
    rc = dload_mod(LINE_MOD, 1U);
    if (rc != 0) return failure(__LINE__, phase);
    /* 硬件维护模表 DLOAD 与 PMODLD 的依赖，程序内部不插入 PSYNC。 */
    rc = pmodld(0U);
    if (rc != 0) return failure(__LINE__, phase);
    if (pfree(P4) != 0) return failure(__LINE__, phase);
    phase = "terminal-psync";
    psync();
    if (wait_irq() != 0) return failure(__LINE__, phase);
    if (check_status() != 0) return failure(__LINE__, phase);
    phase = "clear-completion";
    if (completion_clear() != 0) return failure(__LINE__, phase);
    if (expect_csr(CSR_IRQ, 0U, IRQ_LEVEL) != 0)
        return failure(__LINE__, phase);

    /* 软件只证明命令完成且无 fault；单次接收/反压保持必须看 IT monitor。 */
    phase = "readonly-and-guard";
    if (v2_check_memory(phase) != 0) return failure(__LINE__, phase);
    printf("[HPU][SW-CHECK-PASS] readonly-and-guard=pass window_lines=%u "
           "monitor=needs-monitor; not AXI/handshake coverage\n", WINDOW_LINES);
    return case_pass(__FILE__);
}
