#include <hpu/steps.h>

/*
 * 测试点：IT-CFG-004
 * 目的：STATUS软件可见状态迁移。
 * 模式：定向状态（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    const uint32_t seed = 0u;
    uint32_t status = 0U;
    unsigned timeout;
    int saw_busy = 0;
    int rc;

    /* 准备 producer A/B、模数表和被 poison 的输出，不写任何 CSR。 */
    if (prepare_data(seed) != 0) return 1;
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(HPU_MEM_BASE >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, WINDOW_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (expect_csr(HPU_CSR_BASE_LO_ADDR,
        (uint32_t)HPU_MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(HPU_MEM_BASE >> 32U),
        UINT32_C(0xff)) != 0 ||
        expect_csr(HPU_CSR_SIZE_LO_ADDR,
        WINDOW_LINES, UINT32_MAX) != 0 ||
        expect_csr(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
        return 1;
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return 1;
    if (check_status() != 0) return 1;

    /* DLOAD→DSTORE→PSYNC 的编码与 x10/x11 参数由 inline-asm producer 提供。 */
    rc = dload(P0, LINE_A, POLY_LINES);
    if (rc != 0) return 1;
    rc = dstore_release(P0, LINE_OUT, POLY_LINES);
    if (rc != 0) return 1;
    psync();

    /* 软件必须实际看到 busy，再看到 terminal IRQ 与 idle，避免初始 idle 误判。 */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) return 1;
        if ((status & HPU_STATUS_BUSY) != 0U) saw_busy = 1;
        if (saw_busy && (status & HPU_STATUS_BUSY) == 0U &&
            (hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) != 0U)
            break;
    }
    if (timeout == HPU_TIMEOUT || !saw_busy) return 1;
    if (check_status() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    if (check_regions(LINE_OUT, LINE_A,
        POLY_LINES) != 0)
        return 1;
    /* 更细的逐周期 idle→busy→idle 覆盖仍由 IT monitor 波形判定。 */
    return 0;
}
