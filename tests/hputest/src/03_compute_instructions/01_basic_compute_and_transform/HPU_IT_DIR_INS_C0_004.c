#include <hpu/steps.h>

/*
 * 测试点：IT-INS-C0-004
 * 目的：PMAC有效累加初值闭环。
 * 模式：定向指令闭环（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    const uint32_t seed = UINT32_C(0xc004);
    uint32_t status;
    unsigned timeout;

    /* Data only: producer A/B plus a nonzero 4096-word software accumulator. */
    if (prepare_data(seed) != 0) return 1;
    if (prepare_accumulator(seed, POLY_LINES) != 0) return 1;

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

    /* q0, A, B and accumulator p2 -> PMAC(p2=p2+p0*p1) -> DSTORE. */
    if (dload_mod(LINE_MOD, 1U) != 0) return 1;
    if (pmodld(0U) != 0) return 1;
    if (dload(P0, LINE_A, POLY_LINES) != 0)
        return 1;
    if (dload(P1, LINE_B, POLY_LINES) != 0)
        return 1;
    if (dload(P2, LINE_SCRATCH, POLY_LINES) != 0)
        return 1;
    if (pmac() != 0) return 1;
    if (dstore_release(P2, LINE_OUT, POLY_LINES) != 0)
        return 1;

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

    /* C oracle checks accumulator[i] + A[i]*B[i] modulo q0 for all words. */
    if (check_pmac(LINE_OUT,
        POLY_LINES) != 0) return 1;
    return 0;
}
