#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_STR_002"
#define TESTPOINT "IT-STR-002"
#define DESCRIPTION "8条命令缓存满载与背压恢复"
#define TEST_MODE "定向结构连接"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_STR_QUEUE"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL | HPU_REQ_CACHE_CONTRACT | HPU_REQ_FUNCTION_COVERAGE | HPU_REQ_QUEUE_CONTROL"
#define SEED UINT32_C(0x5102)

int main(void) {
    uint32_t modulus_before[HPU_WORDS_PER_LINE];
    volatile const uint32_t *modulus;
    uint32_t status;
    unsigned command;
    unsigned timeout;
    unsigned word;

    if (hpu_it_prepare_data(SEED) != 0) return 1;
    modulus = hpu_it_line(HPU_IT_LINE_MOD);
    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word)
        modulus_before[word] = modulus[word];

    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    hpu_it_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_IT_MEM_BASE);
    hpu_it_csr_write32(HPU_CSR_BASE_HI_ADDR,
                       (uint32_t)(HPU_IT_MEM_BASE >> 32U));
    hpu_it_csr_write32(HPU_CSR_SIZE_LO_ADDR, HPU_IT_WINDOW_LINES);
    hpu_it_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_it_csr_read32(HPU_CSR_BASE_LO_ADDR) !=
        (uint32_t)HPU_IT_MEM_BASE) return 1;
    if (hpu_it_csr_read32(HPU_CSR_BASE_HI_ADDR) !=
        (uint32_t)(HPU_IT_MEM_BASE >> 32U)) return 1;
    if (hpu_it_csr_read32(HPU_CSR_SIZE_LO_ADDR) != HPU_IT_WINDOW_LINES)
        return 1;
    if (hpu_it_csr_read32(HPU_CSR_SIZE_HI_ADDR) != 0U) return 1;
    hpu_it_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (hpu_it_wait_window_valid(1) != 0) return 1;

    /* One custom1 DLOAD followed by exactly eight queued custom0 commands. */
    if (hpu_it_issue_dload(HPU_IT_P4, HPU_IT_LINE_MOD, 1U,
                           HPU_IT_DMA_MOD_TABLE) != 0) return 1;
    for (command = 0U; command < 8U; ++command) {
        if (hpu_it_issue_pmodld(0U) != 0) return 1;
    }

    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_it_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT) return 1;
    status = hpu_it_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U) return 1;
    if ((status & (HPU_STATUS_BUSY | HPU_STATUS_FAULT_VALID)) != 0U)
        return 1;
    if ((hpu_it_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        return 1;

    /* Software proves completion/no corruption; queue depth needs IT monitor. */
    if (hpu_it_compare_line(HPU_IT_LINE_MOD, modulus_before,
                            HPU_WORDS_PER_LINE) != 0) return 1;
    return 0;
}
