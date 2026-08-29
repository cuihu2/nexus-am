#include <hpu/steps.h>

/*
 * 测试点：IT-PATH-001
 * 目的：custom0命令单次接收与反压保持。
 * 模式：定向通路（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    const uint32_t seed = 0u;
    int rc;

    /* data-only：ELF 中的两组 4096-word 输入和模数记录先落到 DDR。 */
    if (prepare_data(seed) != 0) return 1;
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_FAULT_ADDR, 0U,
        HPU_FAULT_VALID) != 0 ||
        expect_csr(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

    /* HPU 初始化只包含 BASE、SIZE、COMMIT；四个 shadow CSR 均读回。 */
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

    /* custom1 DLOAD 模数表后，custom0 PMODLD(0) 是本例被观察命令。 */
    rc = dload_mod(LINE_MOD, 1U);
    if (rc != 0) return 1;
    rc = pmodld(0U);
    if (rc != 0) return 1;
    psync();
    if (wait_irq() != 0) return 1;
    if (check_status() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

    /* 软件只证明命令完成且无 fault；单次接收/反压保持必须看 IT monitor。 */
    return 0;
}
