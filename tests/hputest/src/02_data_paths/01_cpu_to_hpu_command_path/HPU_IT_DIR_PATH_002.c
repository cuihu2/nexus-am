#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_PATH_002"
#define TESTPOINT "IT-PATH-002"
#define DESCRIPTION "custom1命令与line参数逐笔配对"
#define TEST_MODE "定向通路"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_PATH_CUSTOM1"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL | HPU_REQ_CACHE_CONTRACT"
#define SEED 0u

int main(void) {
    int rc;

    if (hpu_it_prepare_data(SEED) != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_FAULT_ADDR, 0U,
                            HPU_FAULT_VALID) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

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

    /* 四条 custom1 的 x10/x11 分别绑定 A、B、OUT_A、OUT_B 的 line/count。 */
    rc = hpu_it_issue_dload(HPU_IT_P0, HPU_IT_LINE_SRC_A,
                            HPU_IT_POLY_LINES, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dload(HPU_IT_P1, HPU_IT_LINE_SRC_B,
                            HPU_IT_POLY_LINES, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dstore(HPU_IT_P0, HPU_IT_LINE_OUT_A,
                             HPU_IT_POLY_LINES, HPU_IT_STORE_RELEASE);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dstore(HPU_IT_P1, HPU_IT_LINE_OUT_B,
                             HPU_IT_POLY_LINES, HPU_IT_STORE_RELEASE);
    if (rc != 0) return 1;
    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    if (hpu_it_check_final_status() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* 两组对象各比较 4096 个系数，防止 line 参数串线仍然返回 PASS。 */
    if (hpu_it_compare_regions(HPU_IT_LINE_OUT_A, HPU_IT_LINE_SRC_A,
                               HPU_IT_POLY_LINES) != 0)
        return 1;
    if (hpu_it_compare_regions(HPU_IT_LINE_OUT_B, HPU_IT_LINE_SRC_B,
                               HPU_IT_POLY_LINES) != 0)
        return 1;
    /* cycle-accurate 配对与反压证据仍由 IT monitor 给出。 */
    return 0;
}
