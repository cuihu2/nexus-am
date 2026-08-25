#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_STING_STR_001"
#define TESTPOINT "IT-STR-001"
#define DESCRIPTION "分通道反压与CDC约束随机压力"
#define TEST_MODE "STING约束随机结构连接"
#define PRIORITY 3
#define CASE_KIND "HPU_CASE_STR_CHANNELS"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL | HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_FUNCTION_COVERAGE | HPU_REQ_STING"
#define SEED UINT32_C(0xb0a93c42)

int main(void) {
    uint32_t status;
    unsigned timeout;

    /* Deterministic software-visible payload for the first channel sample. */
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

    /* Sample 1: custom1 DLOAD/DSTORE for p0, then custom0 PSYNC. */
    if (hpu_it_issue_dload(HPU_IT_P0, HPU_IT_LINE_SRC_A,
                           HPU_IT_POLY_LINES, HPU_IT_DMA_POLY) != 0)
        return 1;
    if (hpu_it_issue_dstore(HPU_IT_P0, HPU_IT_LINE_OUT_A,
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
    if (hpu_it_compare_regions(HPU_IT_LINE_OUT_A, HPU_IT_LINE_SRC_A,
                               HPU_IT_POLY_LINES) != 0) return 1;

    /* Sample 2 uses a new data-only seed and object p1 on the same window. */
    if (hpu_it_prepare_data(SEED ^ UINT32_C(0x01010101)) != 0) return 1;
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

    if (hpu_it_issue_dload(HPU_IT_P1, HPU_IT_LINE_SRC_B,
                           HPU_IT_POLY_LINES, HPU_IT_DMA_POLY) != 0)
        return 1;
    if (hpu_it_issue_dstore(HPU_IT_P1, HPU_IT_LINE_OUT_B,
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
    if (hpu_it_compare_regions(HPU_IT_LINE_OUT_B, HPU_IT_LINE_SRC_B,
                               HPU_IT_POLY_LINES) != 0) return 1;

    /* STING replay/backpressure/CDC evidence is intentionally monitor-side. */
    return 0;
}
