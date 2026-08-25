#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_CFG_004"
#define TESTPOINT "IT-CFG-004"
#define DESCRIPTION "STATUS软件可见状态迁移"
#define TEST_MODE "定向状态"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_CFG_STATUS"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
#define SEED 0u

int main(void) {
    uint32_t status = 0U;
    unsigned timeout;
    int saw_busy = 0;
    int rc;

    /* 准备 producer A/B、模数表和被 poison 的输出，不写任何 CSR。 */
    if (hpu_it_prepare_data(SEED) != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    hpu_it_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_IT_MEM_BASE);
    hpu_it_csr_write32(HPU_CSR_BASE_HI_ADDR,
                       (uint32_t)(HPU_IT_MEM_BASE >> 32U));
    hpu_it_csr_write32(HPU_CSR_SIZE_LO_ADDR, HPU_IT_WINDOW_LINES);
    hpu_it_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_BASE_LO_ADDR,
                            (uint32_t)HPU_IT_MEM_BASE, UINT32_MAX) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_BASE_HI_ADDR,
                            (uint32_t)(HPU_IT_MEM_BASE >> 32U),
                            UINT32_C(0xff)) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_SIZE_LO_ADDR,
                            HPU_IT_WINDOW_LINES, UINT32_MAX) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
        return 1;
    hpu_it_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (hpu_it_wait_window_valid(1) != 0) return 1;
    if (hpu_it_check_final_status() != 0) return 1;

    /* DLOAD→DSTORE→PSYNC 的编码与 x10/x11 参数由 inline-asm producer 提供。 */
    rc = hpu_it_issue_dload(HPU_IT_P0, HPU_IT_LINE_SRC_A,
                            HPU_IT_POLY_LINES, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dstore(HPU_IT_P0, HPU_IT_LINE_OUT_A,
                             HPU_IT_POLY_LINES, HPU_IT_STORE_RELEASE);
    if (rc != 0) return 1;
    hpu_it_issue_psync();

    /* 软件必须实际看到 busy，再看到 terminal IRQ 与 idle，避免初始 idle 误判。 */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_it_csr_read32(HPU_CSR_STATUS_ADDR);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) return 1;
        if ((status & HPU_STATUS_BUSY) != 0U) saw_busy = 1;
        if (saw_busy && (status & HPU_STATUS_BUSY) == 0U &&
            (hpu_it_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) != 0U)
            break;
    }
    if (timeout == HPU_TIMEOUT || !saw_busy) return 1;
    if (hpu_it_check_final_status() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    if (hpu_it_compare_regions(HPU_IT_LINE_OUT_A, HPU_IT_LINE_SRC_A,
                               HPU_IT_POLY_LINES) != 0)
        return 1;
    /* 更细的逐周期 idle→busy→idle 覆盖仍由 IT monitor 波形判定。 */
    return 0;
}
