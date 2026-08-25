#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_CMB_009"
#define TESTPOINT "IT-CMB-009"
#define DESCRIPTION "Poseidon算法库HADD组合序列"
#define TEST_MODE "定向算法库算子"
#define PRIORITY 2
#define CASE_KIND "HPU_CASE_CMB_HADD"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
#define SEED UINT32_C(0xc009a)

int main(void) {
    uint32_t status;
    unsigned timeout;

    /* HADD's current producer-backed mapping is full-width modular PADD. */
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

    /* Producer sequence for HADD: load q0/A/B, PADD, then DSTORE p2. */
    if (hpu_it_issue_dload(HPU_IT_P4, HPU_IT_LINE_MOD, 1U,
                           HPU_IT_DMA_MOD_TABLE) != 0) return 1;
    if (hpu_it_issue_pmodld(0U) != 0) return 1;
    if (hpu_it_issue_dload(HPU_IT_P0, HPU_IT_LINE_SRC_A,
                           HPU_IT_POLY_LINES, HPU_IT_DMA_POLY) != 0)
        return 1;
    if (hpu_it_issue_dload(HPU_IT_P1, HPU_IT_LINE_SRC_B,
                           HPU_IT_POLY_LINES, HPU_IT_DMA_POLY) != 0)
        return 1;
    if (hpu_it_issue_arith(HPU_IT_ARITH_PADD) != 0) return 1;
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

    /* Compare all 4096 HADD output words with the independent C oracle. */
    if (hpu_it_compare_binary_output(HPU_IT_ARITH_PADD,
                                     HPU_IT_LINE_OUT_A,
                                     HPU_IT_POLY_LINES) != 0) return 1;
    return 0;
}
