#include <hpu/steps.h>

/*
 * 测试点：IT-PATH-003
 * 目的：DDR经DLOAD进入对象并回环验证。
 * 模式：定向数据通路（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    const uint32_t seed = 0u;
    int rc;

    /* A/B 各 4096×32-bit；OUT_A 已被 poison，不能靠预存相同数据误通过。 */
    if (prepare_data(seed) != 0) return 1;
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_FAULT_ADDR, 0U,
        HPU_FAULT_VALID) != 0 ||
        expect_csr(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

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

    /* DDR[A] -> producer DLOAD p0 -> producer DSTORE p0 -> DDR[OUT_A]。 */
    rc = dload(P0, LINE_A, POLY_LINES);
    if (rc != 0) return 1;
    rc = dstore_release(P0, LINE_OUT, POLY_LINES);
    if (rc != 0) return 1;
    psync();
    if (wait_irq() != 0) return 1;
    if (check_status() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    invalidate_lines(LINE_OUT, POLY_LINES);
    if (check_regions(LINE_OUT, LINE_A,
        POLY_LINES) != 0)
        return 1;
    return 0;
}
