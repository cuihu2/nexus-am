#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-STR-006
 * 目的：命令缓存满载期间普通RISC-V指令交叉保序。
 * 模式：定向结构连接+功能覆盖（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_EXTERNAL_ENTRY
 *   - HPU_REQ_FUNCTION_COVERAGE
 *   - HPU_REQ_QUEUE_CONTROL
 */

int main(void) {
    case_start(__FILE__);
    const uint32_t seed = UINT32_C(0x5106);
    uint32_t modulus_before[HPU_WORDS_PER_LINE];
    volatile const uint32_t *modulus;
    volatile uint32_t cpu_sink = 0U;
    uint32_t status;
    unsigned command;
    unsigned cpu_step;
    unsigned timeout;
    unsigned word;

    if (prepare_data(seed) != 0) return case_fail(__FILE__, __LINE__);
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
        (uint32_t)HPU_MEM_BASE) return case_fail(__FILE__, __LINE__);
    if (hpu_csr_read32(HPU_CSR_BASE_HI_ADDR) !=
        (uint32_t)(HPU_MEM_BASE >> 32U)) return case_fail(__FILE__, __LINE__);
    if (hpu_csr_read32(HPU_CSR_SIZE_LO_ADDR) != WINDOW_LINES)
        return case_fail(__FILE__, __LINE__);
    if (hpu_csr_read32(HPU_CSR_SIZE_HI_ADDR) != 0U) return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return case_fail(__FILE__, __LINE__);

    if (dload_mod(LINE_MOD, 1U) != 0) return case_fail(__FILE__, __LINE__);
    for (command = 0U; command < 8U; ++command) {
        uint32_t cpu_value = seed ^ command;

        /* One queued HPU command followed by ordinary RISC-V integer work. */
        if (pmodld(0U) != 0) return case_fail(__FILE__, __LINE__);
        for (cpu_step = 0U; cpu_step < 128U; ++cpu_step) {
            cpu_value ^= cpu_value << 13;
            cpu_value ^= cpu_value >> 17;
            cpu_value ^= cpu_value << 5;
        }
        cpu_sink = cpu_value;
    }

    psync();
    if (wait_irq() != 0) return case_fail(__FILE__, __LINE__);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT) return case_fail(__FILE__, __LINE__);
    status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U) return case_fail(__FILE__, __LINE__);
    if ((status & (HPU_STATUS_BUSY | HPU_STATUS_FAULT_VALID)) != 0U)
        return case_fail(__FILE__, __LINE__);
    if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        return case_fail(__FILE__, __LINE__);

    /* CPU and DDR software oracles; exact queue ordering remains monitor-side. */
    if (cpu_sink != UINT32_C(0xba61d264)) return case_fail(__FILE__, __LINE__);
    if (check_line(LINE_MOD, modulus_before,
        HPU_WORDS_PER_LINE) != 0) return case_fail(__FILE__, __LINE__);
    return case_pass(__FILE__);
}
