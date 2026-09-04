#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-PATH-001
 * 目的：custom0合法命令间隔与反压约束随机。
 * 模式：STING约束随机（P2）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_EXTERNAL_ENTRY
 *   - HPU_REQ_STING
 */

int main(void) {
    case_start(__FILE__);
    const uint32_t seed = 0xF83138C9u;
    unsigned repeat;
    int rc;

    if (prepare_data(seed) != 0) return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_FAULT_ADDR, 0U,
        HPU_FAULT_VALID) != 0 ||
        expect_csr(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
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
    if (check_status() != 0) return case_fail(__FILE__, __LINE__);

    rc = dload_mod(LINE_MOD, 1U);
    if (rc != 0) return case_fail(__FILE__, __LINE__);
    /* 八条 producer PMODLD(0) 提供确定性命令流；随机间隔由 STING 外部控制。 */
    for (repeat = 0U; repeat < 8U; ++repeat) {
        rc = pmodld(0U);
        if (rc != 0) return case_fail(__FILE__, __LINE__);
    }
    psync();
    if (wait_irq() != 0) return case_fail(__FILE__, __LINE__);
    if (check_status() != 0) return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (expect_csr(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return case_fail(__FILE__, __LINE__);

    /* return 0 不代表 STING 覆盖率达标；那部分必须由外部报告验收。 */
    return case_pass(__FILE__);
}
