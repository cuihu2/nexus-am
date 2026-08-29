#include <hpu/steps.h>

/*
 * 测试点：IT-STR-001
 * 目的：custom0与custom1分通道反压及CDC定向。
 * 模式：定向结构连接（P1）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_FUNCTION_COVERAGE
 */

int main(void) {
    const uint32_t seed = UINT32_C(0x5002);
    uint32_t status;
    unsigned timeout;

    if (prepare_data(seed) != 0) return 1;

    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(HPU_MEM_BASE >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, WINDOW_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_csr_read32(HPU_CSR_BASE_LO_ADDR) !=
        (uint32_t)HPU_MEM_BASE) return 1;
    if (hpu_csr_read32(HPU_CSR_BASE_HI_ADDR) !=
        (uint32_t)(HPU_MEM_BASE >> 32U)) return 1;
    if (hpu_csr_read32(HPU_CSR_SIZE_LO_ADDR) != WINDOW_LINES)
        return 1;
    if (hpu_csr_read32(HPU_CSR_SIZE_HI_ADDR) != 0U) return 1;
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return 1;

    /* Four producer DMA commands exercise x10/x11 custom1 traffic. */
    if (dload(P0, LINE_A, POLY_LINES) != 0)
        return 1;
    if (dload(P1, LINE_B, POLY_LINES) != 0)
        return 1;
    if (dstore_release(P0, LINE_OUT, POLY_LINES) != 0)
        return 1;
    if (dstore_release(P1, LINE_OUT_B, POLY_LINES) != 0)
        return 1;

    /* PSYNC supplies custom0 traffic; ready/CDC evidence remains monitor-side. */
    psync();
    if (wait_irq() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT) return 1;
    status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U) return 1;
    if ((status & (HPU_STATUS_BUSY | HPU_STATUS_FAULT_VALID)) != 0U)
        return 1;
    if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        return 1;

    /* Software closes both 4096-word DMA paths; monitor checks channel timing. */
    if (check_regions(LINE_OUT, LINE_A,
        POLY_LINES) != 0) return 1;
    if (check_regions(LINE_OUT_B, LINE_B,
        POLY_LINES) != 0) return 1;
    return 0;
}
