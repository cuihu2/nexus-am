#include <hpu/steps.h>

/*
 * 测试点：IT-STR-002
 * 目的：8条命令缓存满载与背压恢复。
 * 模式：定向结构连接（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_FUNCTION_COVERAGE
 *   - HPU_REQ_QUEUE_CONTROL
 */

int main(void) {
    const uint32_t seed = UINT32_C(0x5102);
    uint32_t modulus_before[HPU_WORDS_PER_LINE];
    volatile const uint32_t *modulus;
    uint32_t status;
    unsigned command;
    unsigned timeout;
    unsigned word;

    if (prepare_data(seed) != 0) return 1;
    modulus = ddr_line(LINE_MOD);
    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word)
        modulus_before[word] = modulus[word];

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

    /* One custom1 DLOAD followed by exactly eight queued custom0 commands. */
    if (dload_mod(LINE_MOD, 1U) != 0) return 1;
    for (command = 0U; command < 8U; ++command) {
        if (pmodld(0U) != 0) return 1;
    }

    psync();
    if (wait_irq() != 0) return 1;
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

    /* Software proves completion/no corruption; queue depth needs IT monitor. */
    if (check_line(LINE_MOD, modulus_before,
        HPU_WORDS_PER_LINE) != 0) return 1;
    return 0;
}
