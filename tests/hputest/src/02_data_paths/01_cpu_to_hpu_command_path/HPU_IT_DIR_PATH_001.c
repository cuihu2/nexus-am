#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_PATH_001"
#define TESTPOINT "IT-PATH-001"
#define DESCRIPTION "custom0命令单次接收与反压保持"
#define TEST_MODE "定向通路"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_PATH_CUSTOM0"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL | HPU_REQ_CACHE_CONTRACT"
#define SEED 0u

int main(void) {
    int rc;

    /* data-only：ELF 中的两组 4096-word 输入和模数记录先落到 DDR。 */
    if (hpu_it_prepare_data(SEED) != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_FAULT_ADDR, 0U,
                            HPU_FAULT_VALID) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

    /* HPU 初始化只包含 BASE、SIZE、COMMIT；四个 shadow CSR 均读回。 */
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

    /* custom1 DLOAD 模数表后，custom0 PMODLD(0) 是本例被观察命令。 */
    rc = hpu_it_issue_dload(HPU_IT_P4, HPU_IT_LINE_MOD, 1U,
                            HPU_IT_DMA_MOD_TABLE);
    if (rc != 0) return 1;
    rc = hpu_it_issue_pmodld(0U);
    if (rc != 0) return 1;
    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    if (hpu_it_check_final_status() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

    /* 软件只证明命令完成且无 fault；单次接收/反压保持必须看 IT monitor。 */
    return 0;
}
