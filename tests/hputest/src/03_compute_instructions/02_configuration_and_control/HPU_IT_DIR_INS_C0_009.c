#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-INS-C0-009
 * 目的：PFREE释放后同对象号复用。
 * 模式：定向对象生命周期（P1）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    case_start(__FILE__);
    const uint32_t seed = UINT32_C(0xc009);
    uint32_t status;
    unsigned timeout;

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

    /* Load A as p0, free p0, reuse p0 for B, and store the reused object. */
    if (dload(P0, LINE_A, POLY_LINES) != 0)
        return case_fail(__FILE__, __LINE__);
    if (pfree(P0) != 0) return case_fail(__FILE__, __LINE__);
    if (dload(P0, LINE_B, POLY_LINES) != 0)
        return case_fail(__FILE__, __LINE__);
    if (dstore_release(P0, LINE_OUT, POLY_LINES) != 0)
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

    /* The output must be all 4096 words of B, not the freed A object. */
    if (check_regions(LINE_OUT, LINE_B,
        POLY_LINES) != 0) return case_fail(__FILE__, __LINE__);
    return case_pass(__FILE__);
}
