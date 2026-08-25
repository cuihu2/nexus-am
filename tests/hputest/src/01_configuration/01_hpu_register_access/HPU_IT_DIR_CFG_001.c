#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_CFG_001"
#define TESTPOINT "IT-CFG-001"
#define DESCRIPTION "HPU相关CSR全量访问、修改与读回"
#define TEST_MODE "定向配置"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_CFG_CSR_ACCESS"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
#define SEED 0u

int main(void) {
    uint32_t status_mask = HPU_STATUS_WINDOW_VALID | HPU_STATUS_BUSY |
                           HPU_STATUS_FAULT_VALID;

    /* 只准备两组 4096×32-bit producer 数据；本函数不访问 HPU CSR。 */
    if (hpu_it_prepare_data(SEED) != 0) return 1;

    /* 逐地址清除旧 fault/IRQ，确保本次读回不继承上一个用例的状态。 */
    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_FAULT_ADDR, 0U,
                            HPU_FAULT_VALID) != 0)
        return 1;
    if (hpu_it_csr_expect32(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

    /* 四个 shadow CSR 均在 main 中按绝对地址写入并读回。 */
    hpu_it_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_IT_MEM_BASE);
    hpu_it_csr_write32(HPU_CSR_BASE_HI_ADDR,
                       (uint32_t)(HPU_IT_MEM_BASE >> 32U));
    hpu_it_csr_write32(HPU_CSR_SIZE_LO_ADDR, HPU_IT_WINDOW_LINES);
    hpu_it_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_BASE_LO_ADDR,
                            (uint32_t)HPU_IT_MEM_BASE, UINT32_MAX) != 0)
        return 1;
    if (hpu_it_csr_expect32(HPU_CSR_BASE_HI_ADDR,
                            (uint32_t)(HPU_IT_MEM_BASE >> 32U),
                            UINT32_C(0xff)) != 0)
        return 1;
    if (hpu_it_csr_expect32(HPU_CSR_SIZE_LO_ADDR,
                            HPU_IT_WINDOW_LINES, UINT32_MAX) != 0)
        return 1;
    if (hpu_it_csr_expect32(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
        return 1;

    /* COMMIT 是写脉冲；STATUS.window-valid 才是配置生效的判据。 */
    hpu_it_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (hpu_it_wait_window_valid(1) != 0) return 1;
    if (hpu_it_csr_expect32(HPU_CSR_STATUS_ADDR,
                            HPU_STATUS_WINDOW_VALID, status_mask) != 0)
        return 1;

    /* PSYNC 产生可读 IRQ；显式 W1C 后再次读回，覆盖最后一个 CSR。 */
    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    if (hpu_it_check_final_status() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

    /* return 0 仅表示上述软件可见 CSR 自检通过；波形覆盖由 IT monitor 判定。 */
    return 0;
}
