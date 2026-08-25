#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_CFG_005"
#define TESTPOINT "IT-CFG-005"
#define DESCRIPTION "FAULT方向对象记录与W1C可观察"
#define TEST_MODE "定向故障可观察"
#define PRIORITY 2
#define CASE_KIND "HPU_CASE_CFG_FAULT"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_FAULT_INJECTION | HPU_REQ_CACHE_CONTRACT"
#define SEED 0u

#define FAULT_IS_LOAD (1U << 1)
#define FAULT_OBJECT_MASK (7U << 4)

int main(void) {
    const uint32_t expected_fault = HPU_FAULT_VALID | FAULT_IS_LOAD;
    uint32_t fault = 0U;
    unsigned timeout;

    if (hpu_it_prepare_data(SEED) != 0) return 1;

    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_FAULT_ADDR, 0U,
                            HPU_FAULT_VALID) != 0)
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

    /* x10=512 已越过合法 [0,512) window；producer DLOAD p0 应记录 load/p0 fault。 */
    hpu_it_issue_raw_dload_p0(HPU_IT_WINDOW_LINES, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        fault = hpu_it_csr_read32(HPU_CSR_FAULT_ADDR);
        if ((fault & HPU_FAULT_VALID) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return 1;
    if ((fault & (HPU_FAULT_VALID | FAULT_IS_LOAD | FAULT_OBJECT_MASK)) !=
        expected_fault)
        return 1;

    /* FAULT_STATUS[0] 为 W1C；轮询确认清除，不能用 printf 当判据。 */
    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_it_csr_read32(HPU_CSR_FAULT_ADDR) &
             HPU_FAULT_VALID) == 0U)
            break;
    }
    if (timeout == HPU_TIMEOUT) return 1;
    if (hpu_it_check_final_status() != 0) return 1;

    /* 外部 fault-injection 入口和波形覆盖仍由 IT/VCS 环境另行判定。 */
    return 0;
}
