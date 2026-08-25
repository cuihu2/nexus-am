#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_PATH_004"
#define TESTPOINT "IT-PATH-004"
#define DESCRIPTION "对象及计算结果经DSTORE写回"
#define TEST_MODE "定向数据通路"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_PATH_STORE"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
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

    /* 加载模数并选 context 0，再加载两个完整 RNS 分量。 */
    rc = hpu_it_issue_dload(HPU_IT_P4, HPU_IT_LINE_MOD, 1U,
                            HPU_IT_DMA_MOD_TABLE);
    if (rc != 0) return 1;
    rc = hpu_it_issue_pmodld(0U);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dload(HPU_IT_P0, HPU_IT_LINE_SRC_A,
                            HPU_IT_POLY_LINES, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dload(HPU_IT_P1, HPU_IT_LINE_SRC_B,
                            HPU_IT_POLY_LINES, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;

    /* producer PADD 写 p2；DSTORE p2 后由 terminal PSYNC 产生完成 IRQ。 */
    rc = hpu_it_issue_arith(HPU_IT_ARITH_PADD);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dstore(HPU_IT_P2, HPU_IT_LINE_OUT_A,
                             HPU_IT_POLY_LINES, HPU_IT_STORE_RELEASE);
    if (rc != 0) return 1;
    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    if (hpu_it_check_final_status() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* C 参考模型逐项计算 (A[i] + B[i]) mod q，共比较 4096 项。 */
    if (hpu_it_compare_binary_output(HPU_IT_ARITH_PADD,
                                     HPU_IT_LINE_OUT_A,
                                     HPU_IT_POLY_LINES) != 0)
        return 1;
    return 0;
}
