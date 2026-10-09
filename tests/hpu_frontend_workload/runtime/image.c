#include <workload.h>
#include <hpu/log.h>

volatile struct workload_progress workload_trace;

void phase_mark(unsigned phase) {
    workload_trace.phase = phase;
    LOG_EVENT("[FRONTEND] %s phase=%u\n", CASE_ID, phase);
}

void clean_lines(unsigned lines) {
#ifndef WORKLOAD_HOST_TEST
    uintptr_t end = MEM_BASE + (uintptr_t)lines * LINE_BYTES;
    for (uintptr_t addr = MEM_BASE; addr < end; addr += 64U)
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 2" : : "r"(addr) : "memory");
    mem_fence();
#else
    (void)lines;
#endif
}

void invalidate_lines(unsigned lines) {
#ifndef WORKLOAD_HOST_TEST
    uintptr_t end = MEM_BASE + (uintptr_t)lines * LINE_BYTES;
    for (uintptr_t addr = MEM_BASE; addr < end; addr += 64U)
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 0" : : "r"(addr) : "memory");
    mem_fence();
#else
    (void)lines;
#endif
}

void prepare_image(void) {
    /* 输出区已由接收器毒化；scratch初值保持producer原值，不能预填golden。 */
    volatile uint32_t *ddr = line_ptr(0U);
    for (unsigned word = 0U; word < IMAGE_LINES * LINE_WORDS; ++word)
        ddr[word] = initial_image[word];
    clean_lines(IMAGE_LINES);
}

int case_fail(unsigned line) {
    workload_trace.status = csr_read(CSR_STATUS);
    workload_trace.irq = csr_read(CSR_IRQ);
    workload_trace.fault = csr_read(CSR_FAULT);
    workload_trace.phase = PHASE_FAIL;
    LOG_ERROR("[FRONTEND][FAIL] %s source-line=%u status=0x%x irq=0x%x fault=0x%x\n",
              CASE_ID, line, workload_trace.status, workload_trace.irq, workload_trace.fault);
    return 1;
}

int case_pass(void) {
    phase_mark(PHASE_DONE);
    LOG_EVENT("[FRONTEND][PASS] %s outputs=%u wait-cycles=%lu\n",
              CASE_ID, OUTPUT_COUNT, (unsigned long)workload_trace.wait_cycles);
    return 0;
}
