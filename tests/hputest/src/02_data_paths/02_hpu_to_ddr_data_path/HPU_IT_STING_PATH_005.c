#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_STING_PATH_005"
#define TESTPOINT "IT-PATH-005"
#define DESCRIPTION "多对象多DDR区域隔离与容量边界"
#define TEST_MODE "STING约束随机"
#define PRIORITY 1
#define CASE_KIND "HPU_CASE_PATH_MULTI_OBJECT"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_STING | HPU_REQ_CAPACITY_CONTRACT"
#define SEED 0x62B896C4u

int main(void) {
    int rc;

    /* A、B 是互异的 producer 4096-word 向量，OUT_A/OUT_B 是互异 poison。 */
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

    /* p0/p1 与四个不重叠 64-line DDR 区域构成可自检的多对象命令流。 */
    rc = hpu_it_issue_dload(HPU_IT_P0, HPU_IT_LINE_SRC_A,
                            HPU_IT_POLY_LINES, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dload(HPU_IT_P1, HPU_IT_LINE_SRC_B,
                            HPU_IT_POLY_LINES, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dstore(HPU_IT_P1, HPU_IT_LINE_OUT_B,
                             HPU_IT_POLY_LINES, HPU_IT_STORE_RELEASE);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dstore(HPU_IT_P0, HPU_IT_LINE_OUT_A,
                             HPU_IT_POLY_LINES, HPU_IT_STORE_RELEASE);
    if (rc != 0) return 1;
    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    if (hpu_it_check_final_status() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    if (hpu_it_compare_regions(HPU_IT_LINE_OUT_A, HPU_IT_LINE_SRC_A,
                               HPU_IT_POLY_LINES) != 0)
        return 1;
    if (hpu_it_compare_regions(HPU_IT_LINE_OUT_B, HPU_IT_LINE_SRC_B,
                               HPU_IT_POLY_LINES) != 0)
        return 1;

    /* STING 随机时序和容量上限覆盖率由外部环境判定，不能由本 return 0 代替。 */
    return 0;
}
