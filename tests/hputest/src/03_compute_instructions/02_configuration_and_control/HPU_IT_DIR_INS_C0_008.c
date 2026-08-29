#include <hpu/steps.h>

/*
 * 测试点：IT-INS-C0-008
 * 目的：PSYNC完成IRQ链路等待与清除。
 * 模式：定向同步指令（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_IRQ_OBSERVATION
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    const uint32_t seed = UINT32_C(0xc008);
    uint32_t input_before[HPU_WORDS_PER_LINE];
    volatile const uint32_t *input;
    uint32_t status;
    unsigned timeout;
    unsigned word;

    /* Data only; save one producer line so PSYNC cannot hide memory damage. */
    if (prepare_data(seed) != 0) return 1;
    input = ddr_line(LINE_A);
    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word)
        input_before[word] = input[word];

    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(HPU_MEM_BASE >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, WINDOW_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_csr_read32(HPU_CSR_BASE_LO_ADDR) !=
        (uint32_t)HPU_MEM_BASE) return 1;
    if (hpu_csr_read32(HPU_CSR_BASE_HI_ADDR) !=
        (uint32_t)(HPU_MEM_BASE >> 32U)) return 1;
    if (hpu_csr_read32(HPU_CSR_SIZE_LO_ADDR) != WINDOW_LINES)
        return 1;
    if (hpu_csr_read32(HPU_CSR_SIZE_HI_ADDR) != 0U) return 1;
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return 1;

    /* PSYNC is the command under test; completion must raise the MMIO IRQ. */
    psync();
    if (wait_irq() != 0) return 1;
    status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_BUSY) != 0U) return 1;

    /* HPU_IRQ is W1C.  Verify that the level actually falls after clearing. */
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT) return 1;
    status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U) return 1;
    if ((status & (HPU_STATUS_BUSY | HPU_STATUS_FAULT_VALID)) != 0U)
        return 1;
    if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        return 1;

    /* The protocol oracle also verifies that PSYNC did not modify input DDR. */
    if (check_line(LINE_A, input_before,
        HPU_WORDS_PER_LINE) != 0) return 1;
    return 0;
}
