#include <hpu/steps.h>

/*
 * 测试点：IT-PATH-002
 * 目的：custom1命令与line参数逐笔配对。
 * 模式：定向通路（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    const uint32_t seed = 0u;
    int rc;

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

    /* 四条 custom1 的 x10/x11 分别绑定 A、B、OUT_A、OUT_B 的 line/count。 */
    rc = dload(P0, LINE_A, POLY_LINES);
    if (rc != 0) return 1;
    rc = dload(P1, LINE_B, POLY_LINES);
    if (rc != 0) return 1;
    rc = dstore_release(P0, LINE_OUT, POLY_LINES);
    if (rc != 0) return 1;
    rc = dstore_release(P1, LINE_OUT_B, POLY_LINES);
    if (rc != 0) return 1;
    psync();
    if (wait_irq() != 0) return 1;
    if (check_status() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* 两组对象各比较 4096 个系数，防止 line 参数串线仍然返回 PASS。 */
    if (check_regions(LINE_OUT, LINE_A,
        POLY_LINES) != 0)
        return 1;
    if (check_regions(LINE_OUT_B, LINE_B,
        POLY_LINES) != 0)
        return 1;
    /* cycle-accurate 配对与反压证据仍由 IT monitor 给出。 */
    return 0;
}
