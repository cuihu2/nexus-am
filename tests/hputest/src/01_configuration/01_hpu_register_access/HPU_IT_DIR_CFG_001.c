#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-CFG-001
 * 目的：HPU相关CSR全量访问、修改与读回。
 * 模式：定向配置（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    case_start(__FILE__);
    const uint32_t seed = 0u;
    uint32_t status_mask = HPU_STATUS_WINDOW_VALID | HPU_STATUS_BUSY |
        HPU_STATUS_FAULT_VALID;

    /* 只准备两组 4096×32-bit producer 数据；本函数不访问 HPU CSR。 */
    if (prepare_data(seed) != 0) return case_fail(__FILE__, __LINE__);

    /* 逐地址清除旧 fault/IRQ，确保本次读回不继承上一个用例的状态。 */
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_FAULT_ADDR, 0U,
        HPU_FAULT_VALID) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return case_fail(__FILE__, __LINE__);

    /* 四个 shadow CSR 均在 main 中按绝对地址写入并读回。 */
    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(HPU_MEM_BASE >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, WINDOW_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (expect_csr(HPU_CSR_BASE_LO_ADDR,
        (uint32_t)HPU_MEM_BASE, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(HPU_MEM_BASE >> 32U),
        UINT32_C(0xff)) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(HPU_CSR_SIZE_LO_ADDR,
        WINDOW_LINES, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    if (expect_csr(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
        return case_fail(__FILE__, __LINE__);

    /* COMMIT 是写脉冲；STATUS.window-valid 才是配置生效的判据。 */
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return case_fail(__FILE__, __LINE__);
    if (expect_csr(HPU_CSR_STATUS_ADDR,
        HPU_STATUS_WINDOW_VALID, status_mask) != 0)
        return case_fail(__FILE__, __LINE__);

    /* PSYNC 产生可读 IRQ；显式 W1C 后再次读回，覆盖最后一个 CSR。 */
    psync();
    if (wait_irq() != 0) return case_fail(__FILE__, __LINE__);
    if (check_status() != 0) return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return case_fail(__FILE__, __LINE__);

    /* return 0 仅表示上述软件可见 CSR 自检通过；波形覆盖由 IT monitor 判定。 */
    return case_pass(__FILE__);
}
