#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_INS_C0_008"
#define TESTPOINT "IT-INS-C0-008"
#define DESCRIPTION "PSYNC完成IRQ链路等待与清除"
#define TEST_MODE "定向同步指令"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_INS_PSYNC"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_IRQ_OBSERVATION | HPU_REQ_CACHE_CONTRACT"
#define SEED UINT32_C(0xc008)

int main(void) {
    uint32_t input_before[HPU_WORDS_PER_LINE];
    volatile const uint32_t *input;
    uint32_t status;
    unsigned timeout;
    unsigned word;

    /* Data only; save one producer line so PSYNC cannot hide memory damage. */
    if (hpu_it_prepare_data(SEED) != 0) return 1;
    input = hpu_it_line(HPU_IT_LINE_SRC_A);
    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word)
        input_before[word] = input[word];

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

    /* PSYNC is the command under test; completion must raise the MMIO IRQ. */
    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    status = hpu_it_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_BUSY) != 0U) return 1;

    /* HPU_IRQ is W1C.  Verify that the level actually falls after clearing. */
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

    /* The protocol oracle also verifies that PSYNC did not modify input DDR. */
    if (hpu_it_compare_line(HPU_IT_LINE_SRC_A, input_before,
                            HPU_WORDS_PER_LINE) != 0) return 1;
    return 0;
}
