#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-CFG-006
 * 目的：PSYNC完成状态等待、IRQ清除与重触发。
 * 模式：定向完成事件（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_IRQ_OBSERVATION
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    case_start(__FILE__);
    const uint32_t seed = 0u;
    unsigned event;
    unsigned timeout;

    if (prepare_data(seed) != 0) return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

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

    /* 两次独立 PSYNC：每次都必须置位、W1C 清除，并能再次置位。 */
    for (event = 0U; event < 2U; ++event) {
        psync();
        if (wait_irq() != 0) return case_fail(__FILE__, __LINE__);
        if (check_status() != 0) return case_fail(__FILE__, __LINE__);
        hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
        hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
        for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
            if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) &
                 HPU_IRQ_LEVEL) == 0U)
                break;
        }
        if (timeout == HPU_TIMEOUT) return case_fail(__FILE__, __LINE__);
    }

    /* 本例轮询 MMIO；真实 PLIC 中断入口与波形证据不在 C 中假验收。 */
    return case_pass(__FILE__);
}
