#include <hpu/steps.h>

/*
 * 测试点：IT-CFG-003
 * 目的：窗口内地址与传输长度换算。
 * 模式：定向配置（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    const uint32_t seed = 0u;
    enum { LAST_POLY_LINE = WINDOW_LINES - POLY_LINES };
    unsigned line;
    int rc;

    /* 两组输入均来自 inline-asm producer，A 用于本次 4096-word 回环。 */
    if (prepare_data(seed) != 0) return 1;
    for (line = 0U; line < POLY_LINES; ++line)
        poison_line(LAST_POLY_LINE + line,
        UINT32_C(0xc1030000) ^ line);
    clean_lines(LAST_POLY_LINE, POLY_LINES);

    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* BASE/SIZE 单位分别为 byte 地址和 256-byte line 数。 */
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

    /* x10=0/448，x11=64：覆盖窗口首行到最后一个合法 64-line 区间。 */
    rc = dload(P0, LINE_A, POLY_LINES);
    if (rc != 0) return 1;
    rc = dstore_release(P0, LAST_POLY_LINE, POLY_LINES);
    if (rc != 0) return 1;
    psync();
    if (wait_irq() != 0) return 1;
    if (check_status() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* 逐个比较 64 lines × 64 words，即完整 4096 个 32-bit 系数。 */
    if (check_regions(LAST_POLY_LINE, LINE_A,
        POLY_LINES) != 0)
        return 1;
    return 0;
}
