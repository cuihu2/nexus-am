#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_STR_006"
#define TESTPOINT "IT-STR-006"
#define DESCRIPTION "命令缓存满载期间普通RISC-V指令交叉保序"
#define TEST_MODE "定向结构连接+功能覆盖"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_STR_QUEUE_MIXED"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL | HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_FUNCTION_COVERAGE | HPU_REQ_QUEUE_CONTROL"
#define SEED UINT32_C(0x5106)

int main(void) {
    uint32_t modulus_before[HPU_WORDS_PER_LINE];
    volatile const uint32_t *modulus;
    volatile uint32_t cpu_sink = 0U;
    uint32_t status;
    unsigned command;
    unsigned cpu_step;
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

    if (hpu_it_issue_dload(HPU_IT_P4, HPU_IT_LINE_MOD, 1U,
                           HPU_IT_DMA_MOD_TABLE) != 0) return 1;
    for (command = 0U; command < 8U; ++command) {
        uint32_t cpu_value = SEED ^ command;

        /* One queued HPU command followed by ordinary RISC-V integer work. */
        if (hpu_it_issue_pmodld(0U) != 0) return 1;
        for (cpu_step = 0U; cpu_step < 128U; ++cpu_step) {
            cpu_value ^= cpu_value << 13;
            cpu_value ^= cpu_value >> 17;
            cpu_value ^= cpu_value << 5;
        }
        cpu_sink = cpu_value;
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

    /* CPU and DDR software oracles; exact queue ordering remains monitor-side. */
    if (cpu_sink != UINT32_C(0xba61d264)) return 1;
    if (hpu_it_compare_line(HPU_IT_LINE_MOD, modulus_before,
                            HPU_WORDS_PER_LINE) != 0) return 1;
    return 0;
}
