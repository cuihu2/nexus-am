#include <hpu/steps.h>

/*
 * 测试点：IT-CFG-002
 * 目的：配置写序与COMMIT间隔约束随机。
 * 模式：STING约束随机（P2）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_EXTERNAL_ENTRY
 *   - HPU_REQ_STING
 */

int main(void) {
    const uint32_t seed = 0x04BEA183u;
    static const unsigned offsets[] = {0U, 32U, 64U, 16U, 0U};
    unsigned index;

    /* 固定 seed 只负责 data-only fixture；STING 激励仍由外部入口提供。 */
    if (prepare_data(seed) != 0) return 1;

    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_FAULT_ADDR, 0U,
        HPU_FAULT_VALID) != 0 ||
        expect_csr(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

    /*
     * 偶数轮和奇数轮使用不同 shadow 写序；每轮 COMMIT 前都逐项读回。
     * 这只自检合法软件序列，随机间隔/反压覆盖率不能由 C 代码冒充。
     */
    for (index = 0U; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        uintptr_t base = HPU_MEM_BASE +
        (uintptr_t)offsets[index] * HPU_LINE_BYTES;
        uint32_t lines = WINDOW_LINES - offsets[index];

        if ((index & 1U) == 0U) {
            hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)base);
            hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(base >> 32U));
            hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, lines);
            hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
        } else {
            hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
            hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(base >> 32U));
            hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, lines);
            hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)base);
        }
        if (expect_csr(HPU_CSR_BASE_LO_ADDR, (uint32_t)base,
        UINT32_MAX) != 0 ||
            expect_csr(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(base >> 32U),
        UINT32_C(0xff)) != 0 ||
            expect_csr(HPU_CSR_SIZE_LO_ADDR, lines,
        UINT32_MAX) != 0 ||
            expect_csr(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
            return 1;

        hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
        if (wait_window(1) != 0) return 1;
        if (check_status() != 0) return 1;
    }

    return 0;
}
