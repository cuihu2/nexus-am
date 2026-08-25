#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_INS_C0_007"
#define TESTPOINT "IT-INS-C0-007"
#define DESCRIPTION "PMODLD模上下文切换效果"
#define TEST_MODE "定向配置指令"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_INS_PMODLD"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
#define SEED UINT32_C(0xc007)

static uint32_t c_mod_mul(uint32_t left, uint32_t right, uint32_t modulus) {
    return (uint32_t)(((uint64_t)left * right) % modulus);
}

int main(void) {
    volatile const uint32_t *input_a;
    volatile const uint32_t *input_b;
    volatile const uint32_t *output;
    uint32_t status;
    unsigned timeout;
    unsigned word;

    /* Data only: q0 is record 0, q1 is producer-compatible record 6. */
    if (hpu_it_prepare_data(SEED) != 0) return 1;

    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    hpu_it_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_IT_MEM_BASE);
    hpu_it_csr_write32(HPU_CSR_BASE_HI_ADDR,
                       (uint32_t)(HPU_IT_MEM_BASE >> 32U));
    hpu_it_csr_write32(HPU_CSR_SIZE_LO_ADDR, HPU_IT_WINDOW_LINES);
    hpu_it_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_it_csr_read32(HPU_CSR_BASE_LO_ADDR) !=
        (uint32_t)HPU_IT_MEM_BASE) return 1;
    if (hpu_it_csr_read32(HPU_CSR_BASE_HI_ADDR) !=
        (uint32_t)(HPU_IT_MEM_BASE >> 32U)) return 1;
    if (hpu_it_csr_read32(HPU_CSR_SIZE_LO_ADDR) != HPU_IT_WINDOW_LINES)
        return 1;
    if (hpu_it_csr_read32(HPU_CSR_SIZE_HI_ADDR) != 0U) return 1;
    hpu_it_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (hpu_it_wait_window_valid(1) != 0) return 1;

    /* Load the table, select record 6 (q1), then make PMUL observe q1. */
    if (hpu_it_issue_dload(HPU_IT_P4, HPU_IT_LINE_MOD, 1U,
                           HPU_IT_DMA_MOD_TABLE) != 0) return 1;
    if (hpu_it_issue_pmodld(6U) != 0) return 1;
    if (hpu_it_issue_dload(HPU_IT_P0, HPU_IT_LINE_SRC_A,
                           HPU_IT_POLY_LINES, HPU_IT_DMA_POLY) != 0)
        return 1;
    if (hpu_it_issue_dload(HPU_IT_P1, HPU_IT_LINE_SRC_B,
                           HPU_IT_POLY_LINES, HPU_IT_DMA_POLY) != 0)
        return 1;
    if (hpu_it_issue_arith(HPU_IT_ARITH_PMUL) != 0) return 1;
    if (hpu_it_issue_dstore(HPU_IT_P2, HPU_IT_LINE_OUT_A,
                            HPU_IT_POLY_LINES, HPU_IT_STORE_RELEASE) != 0)
        return 1;

    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_it_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT) return 1;
    status = hpu_it_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U) return 1;
    if ((status & (HPU_STATUS_BUSY | HPU_STATUS_FAULT_VALID)) != 0U)
        return 1;
    if ((hpu_it_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        return 1;

    /* q1 oracle: compare every HPU coefficient with C (A*B mod q1). */
    hpu_it_invalidate_lines(HPU_IT_LINE_OUT_A, HPU_IT_POLY_LINES);
    input_a = hpu_it_line(HPU_IT_LINE_SRC_A);
    input_b = hpu_it_line(HPU_IT_LINE_SRC_B);
    output = hpu_it_line(HPU_IT_LINE_OUT_A);
    for (word = 0U; word < HPU_IT_COEFFICIENTS; ++word) {
        if (output[word] !=
            c_mod_mul(input_a[word], input_b[word], HPU_IT_Q1))
            return 1;
    }
    return 0;
}
