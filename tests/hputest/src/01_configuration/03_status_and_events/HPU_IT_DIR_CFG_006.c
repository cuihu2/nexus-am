#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_CFG_006"
#define TESTPOINT "IT-CFG-006"
#define DESCRIPTION "PSYNC完成状态等待、IRQ清除与重触发"
#define TEST_MODE "定向完成事件"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_CFG_IRQ"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_IRQ_OBSERVATION | HPU_REQ_CACHE_CONTRACT"
#define SEED 0u

int main(void) {
    unsigned event;
    unsigned timeout;

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

    /* 两次独立 PSYNC：每次都必须置位、W1C 清除，并能再次置位。 */
    for (event = 0U; event < 2U; ++event) {
        hpu_it_issue_psync();
        if (hpu_it_wait_irq_level() != 0) return 1;
        if (hpu_it_check_final_status() != 0) return 1;
        hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
        hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
        for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
            if ((hpu_it_csr_read32(HPU_CSR_IRQ_ADDR) &
                 HPU_IRQ_LEVEL) == 0U)
                break;
        }
        if (timeout == HPU_TIMEOUT) return 1;
    }

    /* 本例轮询 MMIO；真实 PLIC 中断入口与波形证据不在 C 中假验收。 */
    return 0;
}
