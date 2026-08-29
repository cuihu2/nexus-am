#include <hpu/steps.h>

/*
 * 测试点：IT-PATH-005
 * 目的：多对象多DDR区域隔离与容量边界。
 * 模式：STING约束随机（P1）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_EXTERNAL_ENTRY
 *   - HPU_REQ_STING
 *   - HPU_REQ_CAPACITY_CONTRACT
 */

int main(void) {
    const uint32_t seed = 0x62B896C4u;
    int rc;

    /* A、B 是互异的 producer 4096-word 向量，OUT_A/OUT_B 是互异 poison。 */
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

    /* p0/p1 与四个不重叠 64-line DDR 区域构成可自检的多对象命令流。 */
    rc = dload(P0, LINE_A, POLY_LINES);
    if (rc != 0) return 1;
    rc = dload(P1, LINE_B, POLY_LINES);
    if (rc != 0) return 1;
    rc = dstore_release(P1, LINE_OUT_B, POLY_LINES);
    if (rc != 0) return 1;
    rc = dstore_release(P0, LINE_OUT, POLY_LINES);
    if (rc != 0) return 1;
    psync();
    if (wait_irq() != 0) return 1;
    if (check_status() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    if (check_regions(LINE_OUT, LINE_A,
        POLY_LINES) != 0)
        return 1;
    if (check_regions(LINE_OUT_B, LINE_B,
        POLY_LINES) != 0)
        return 1;

    /* STING 随机时序和容量上限覆盖率由外部环境判定，不能由本 return 0 代替。 */
    return 0;
}
