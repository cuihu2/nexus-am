#include <hpu/steps.h>

/*
 * 测试点：IT-CFG-002
 * 目的：窗口A/B配置原子切换。
 * 模式：定向配置（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    const uint32_t seed = 0u;
    enum { WINDOW_B_OFFSET = 256U, WINDOW_B_LINES = 256U };
    uint32_t expected[HPU_WORDS_PER_LINE];
    uintptr_t window_b = HPU_MEM_BASE +
        (uintptr_t)WINDOW_B_OFFSET * HPU_LINE_BYTES;
    unsigned word;
    int rc;

    /* producer 的 A/B 各含 4096 个系数；这里只另备一行窗口切换 canary。 */
    if (prepare_data(seed) != 0) return 1;
    fill_line(WINDOW_B_OFFSET, UINT32_C(0xc102b), MOD_Q0);
    poison_line(WINDOW_B_OFFSET + 64U, UINT32_C(0xc1020));
    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word)
        expected[word] = ddr_line(WINDOW_B_OFFSET)[word];
    clean_lines(WINDOW_B_OFFSET, 1U);
    clean_lines(WINDOW_B_OFFSET + 64U, 1U);

    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_FAULT_ADDR, 0U,
        HPU_FAULT_VALID) != 0)
        return 1;

    /* 先提交窗口 A（完整 512 lines），所有 shadow 值逐项读回。 */
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

    /* 再提交窗口 B；相同相对 line 现在必须落到物理 line 256 之后。 */
    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)window_b);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR, (uint32_t)(window_b >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, WINDOW_B_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (expect_csr(HPU_CSR_BASE_LO_ADDR, (uint32_t)window_b,
        UINT32_MAX) != 0 ||
        expect_csr(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(window_b >> 32U),
        UINT32_C(0xff)) != 0 ||
        expect_csr(HPU_CSR_SIZE_LO_ADDR, WINDOW_B_LINES,
        UINT32_MAX) != 0 ||
        expect_csr(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
        return 1;
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return 1;

    /* producer DLOAD/DSTORE 使用 x10=line、x11=count；相对地址保持不变。 */
    rc = dload(P0, 0U, 1U);
    if (rc != 0) return 1;
    rc = dstore_release(P0, 64U, 1U);
    if (rc != 0) return 1;
    psync();
    if (wait_irq() != 0) return 1;
    if (check_status() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* CPU 绝对地址 line 320 的 64 个 word 必须等于窗口 B 的 line 0。 */
    if (check_line(WINDOW_B_OFFSET + 64U, expected,
        HPU_WORDS_PER_LINE) != 0)
        return 1;
    return 0;
}
