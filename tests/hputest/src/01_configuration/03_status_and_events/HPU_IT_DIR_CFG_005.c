#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-CFG-005
 * 目的：FAULT方向对象记录与W1C可观察。
 * 模式：定向故障可观察（P2）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_FAULT_INJECTION
 *   - HPU_REQ_CACHE_CONTRACT
 */

#define FAULT_IS_LOAD (1U << 1)
#define FAULT_OBJECT_MASK (7U << 4)

int main(void) {
    case_start(__FILE__);
    const uint32_t seed = 0u;
    const uint32_t expected_fault = HPU_FAULT_VALID | FAULT_IS_LOAD;
    uint32_t fault = 0U;
    unsigned timeout;

    if (prepare_data(seed) != 0) return case_fail(__FILE__, __LINE__);

    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_FAULT_ADDR, 0U,
        HPU_FAULT_VALID) != 0)
        return case_fail(__FILE__, __LINE__);

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
        return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return case_fail(__FILE__, __LINE__);

    /* x10=512 已越过合法 [0,512) window；producer DLOAD p0 应记录 load/p0 fault。 */
    raw_dload_p0(WINDOW_LINES, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        fault = hpu_csr_read32(HPU_CSR_FAULT_ADDR);
        if ((fault & HPU_FAULT_VALID) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return case_fail(__FILE__, __LINE__);
    if ((fault & (HPU_FAULT_VALID | FAULT_IS_LOAD | FAULT_OBJECT_MASK)) !=
        expected_fault)
        return case_fail(__FILE__, __LINE__);

    /* FAULT_STATUS[0] 为 W1C；轮询确认清除，不能用 printf 当判据。 */
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) &
             HPU_FAULT_VALID) == 0U)
            break;
    }
    if (timeout == HPU_TIMEOUT) return case_fail(__FILE__, __LINE__);
    if (check_status() != 0) return case_fail(__FILE__, __LINE__);

    /* 外部 fault-injection 入口和波形覆盖仍由 IT/VCS 环境另行判定。 */
    return case_pass(__FILE__);
}
