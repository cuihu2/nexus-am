#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-INS-C0-007
 * 目的：PMODLD模上下文切换效果。
 * 模式：定向配置指令（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

static uint32_t c_mod_mul(uint32_t left, uint32_t right, uint32_t modulus) {
    return (uint32_t)(((uint64_t)left * right) % modulus);
}

int main(void) {
    case_start(__FILE__);
    const uint32_t seed = UINT32_C(0xc007);
    volatile const uint32_t *input_a;
    volatile const uint32_t *input_b;
    volatile const uint32_t *output;
    uint32_t status;
    unsigned timeout;
    unsigned word;

    /* Data only: q0 is record 0, q1 is producer-compatible record 6. */
    if (prepare_data(seed) != 0) return case_fail(__FILE__, __LINE__);

    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(HPU_MEM_BASE >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, WINDOW_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_csr_read32(HPU_CSR_BASE_LO_ADDR) !=
        (uint32_t)HPU_MEM_BASE) return case_fail(__FILE__, __LINE__);
    if (hpu_csr_read32(HPU_CSR_BASE_HI_ADDR) !=
        (uint32_t)(HPU_MEM_BASE >> 32U)) return case_fail(__FILE__, __LINE__);
    if (hpu_csr_read32(HPU_CSR_SIZE_LO_ADDR) != WINDOW_LINES)
        return case_fail(__FILE__, __LINE__);
    if (hpu_csr_read32(HPU_CSR_SIZE_HI_ADDR) != 0U) return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return case_fail(__FILE__, __LINE__);

    /* Load the table, select record 6 (q1), then make PMUL observe q1. */
    if (dload_mod(LINE_MOD, 1U) != 0) return case_fail(__FILE__, __LINE__);
    if (pmodld(6U) != 0) return case_fail(__FILE__, __LINE__);
    if (dload(P0, LINE_A, POLY_LINES) != 0)
        return case_fail(__FILE__, __LINE__);
    if (dload(P1, LINE_B, POLY_LINES) != 0)
        return case_fail(__FILE__, __LINE__);
    if (pmul() != 0) return case_fail(__FILE__, __LINE__);
    if (dstore_release(P2, LINE_OUT, POLY_LINES) != 0)
        return case_fail(__FILE__, __LINE__);

    psync();
    if (wait_irq() != 0) return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT) return case_fail(__FILE__, __LINE__);
    status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U) return case_fail(__FILE__, __LINE__);
    if ((status & (HPU_STATUS_BUSY | HPU_STATUS_FAULT_VALID)) != 0U)
        return case_fail(__FILE__, __LINE__);
    if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        return case_fail(__FILE__, __LINE__);

    /* q1 oracle: compare every HPU coefficient with C (A*B mod q1). */
    invalidate_lines(LINE_OUT, POLY_LINES);
    input_a = ddr_line(LINE_A);
    input_b = ddr_line(LINE_B);
    output = ddr_line(LINE_OUT);
    for (word = 0U; word < POLY_WORDS; ++word) {
        if (output[word] !=
            c_mod_mul(input_a[word], input_b[word], MOD_Q1))
            return case_fail(__FILE__, __LINE__);
    }
    return case_pass(__FILE__);
}
