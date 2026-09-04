#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-INS-C0-002
 * 目的：PSUB模减结果写回闭环。
 * 模式：定向指令闭环（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    case_start(__FILE__);
    const uint32_t seed = UINT32_C(0xc002);
    uint32_t status;
    unsigned timeout;

    /* Data only: copy producer A/B, build q0, and poison both output regions. */
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

    /* Producer encodings: q0, A, B, PSUB(p2=p0-p1), then store p2. */
    if (dload_mod(LINE_MOD, 1U) != 0) return case_fail(__FILE__, __LINE__);
    if (pmodld(0U) != 0) return case_fail(__FILE__, __LINE__);
    if (dload(P0, LINE_A, POLY_LINES) != 0)
        return case_fail(__FILE__, __LINE__);
    if (dload(P1, LINE_B, POLY_LINES) != 0)
        return case_fail(__FILE__, __LINE__);
    if (psub() != 0) return case_fail(__FILE__, __LINE__);
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

    /* Software oracle checks all 4096 coefficients modulo q0. */
    if (check_psub(LINE_OUT, POLY_LINES) != 0) return case_fail(__FILE__, __LINE__);
    return case_pass(__FILE__);
}
