#include <hpu/application_case.h>
#include <hpu/log.h>
#include <hpu/report.h>
#include <hpu/trace.h>

typedef uint64_t image_word __attribute__((may_alias));

int application_prepare(void) {
    /* 镜像和 DDR 均 256 B 对齐；一拍搬 64 bit，64 B 一次循环，不改变任何数据。 */
    volatile image_word *memory = (volatile image_word *)ddr_line(0U);
    const image_word *source = (const image_word *)application_window;
    for (unsigned word = 0; word < HPU_APPLICATION_LINES * WORDS_PER_LINE / 2U; word += 8U) {
        memory[word+0] = source[word+0]; memory[word+1] = source[word+1];
        memory[word+2] = source[word+2]; memory[word+3] = source[word+3];
        memory[word+4] = source[word+4]; memory[word+5] = source[word+5];
        memory[word+6] = source[word+6]; memory[word+7] = source[word+7];
    }
    clean_lines(0U, HPU_APPLICATION_LINES);
    return 0;
}

int application_check_results(void) {
    int failed = 0;
    for (unsigned index = 0; index < HPU_APPLICATION_GOLDEN_COUNT; ++index) {
        const struct hpu_application_output *output = &hpu_application_outputs[index];
        invalidate_lines(output->line, output->lines);
        LOG_DEBUG("[HPU][APP][OUTPUT] step=%u component=%u modulus=%u line=%u\n",
                  output->step, output->component, output->modulus_id, output->line);
        const int rc = result_compare("application", ddr_line(output->line),
                                 application_golden + output->golden_word,
                                 output->padded_words, output->modulus);
        if (rc != 0)
            LOG_ERROR("[HPU][APP][LOCATION] step=%u component=%u MOD_ID=%u q=%u line=%u\n",
                      output->step, output->component, output->modulus_id,
                      output->modulus, output->line);
        failed |= rc;
    }
    return failed;
}

int application_check_memory(void) {
    for (unsigned line = 0; line < HPU_APPLICATION_LINES; ++line) {
        if (application_writable[line])
            continue;
        /* 只失效会读取的只读连续区，避免重复扫过所有计算/scratch cache block。 */
        if (line == 0U || application_writable[line-1U]) {
            unsigned end = line + 1U;
            while (end < HPU_APPLICATION_LINES && !application_writable[end]) ++end;
            invalidate_lines(line, end-line);
        }
        volatile const uint32_t *actual = ddr_line(line);
        for (unsigned lane = 0; lane < WORDS_PER_LINE; ++lane) {
            const uint32_t expected = application_window[line * WORDS_PER_LINE + lane];
            const uint32_t value = actual[lane];
            if (value != expected) {
                LOG_ERROR("[HPU][APP][FAIL] phase=readonly-guard line=%u lane=%u "
                          "actual=0x%x expected=0x%x\n",
                          line, lane, value, expected);
                return 1;
            }
        }
    }
    return 0;
}

#ifndef HPU_APPLICATION_TIMEOUT_CYCLES
#define HPU_APPLICATION_TIMEOUT_CYCLES UINT64_C(25000000)
#endif
#ifdef HPU_APPLICATION_HOST_TEST
extern uint64_t application_test_cycle(void);
extern uint32_t application_test_read(uintptr_t address);
#define APP_READ(address) application_test_read(address)
#define APP_CYCLE() application_test_cycle()
#else
#define APP_READ(address) csr_read(address)
static uint64_t application_cycle(void) {
#if defined(__riscv)
    uint64_t cycle;
    __asm__ volatile("rdcycle %0" : "=r"(cycle) : : "memory");
    return cycle;
#else
    return 0; /* 非 RISC-V 构建只用于数据检查，不能执行此硬件入口。 */
#endif
}
#define APP_CYCLE() application_cycle()
#endif

int application_wait(void) {
    const uint64_t start = APP_CYCLE();
    unsigned polls = 0U;
    for (;;) {
        const uint32_t irq = APP_READ(CSR_IRQ);
        const uint32_t status = APP_READ(CSR_STATUS);
        const uint32_t fault = APP_READ(CSR_FAULT);
        if (fault & FAULT_VALID || status & STATUS_FAULT) {
            application_trace.status = status; application_trace.irq = irq;
            application_trace.fault = fault;
            application_trace.wait_cycles = APP_CYCLE() - start;
            LOG_ERROR("[HPU][APP][WAIT-FAIL] reason=fault status=0x%x irq=0x%x fault=0x%x\n",
                      status, irq, fault);
            return 1;
        }
        if ((irq & IRQ_LEVEL) && (status & (STATUS_VALID | STATUS_BUSY)) == STATUS_VALID) {
            application_trace.status = status; application_trace.irq = irq;
            application_trace.fault = fault; application_trace.wait_cycles = APP_CYCLE() - start;
            return 0;
        }
        if ((++polls & 63U) == 0U) {
            const uint64_t elapsed = APP_CYCLE() - start;
            application_trace.status = status; application_trace.irq = irq;
            application_trace.fault = fault; application_trace.wait_cycles = elapsed;
            if (elapsed >= HPU_APPLICATION_TIMEOUT_CYCLES) {
                LOG_ERROR("[HPU][APP][WAIT-FAIL] reason=cycle-timeout cycles=%lu status=0x%x irq=0x%x fault=0x%x\n",
                          (unsigned long)elapsed, status, irq, fault);
                return 1;
            }
        }
    }
}
