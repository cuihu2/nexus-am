#include <workload.h>
#include <hpu/log.h>

#ifndef WAIT_CYCLES
#define WAIT_CYCLES UINT64_C(25000000)
#endif

int expect_csr(uintptr_t address, uint32_t expected) {
    const uint32_t actual = csr_read(address);
    if (actual == expected) return 0;
    LOG_ERROR("[FRONTEND][CSR] addr=0x%lx actual=0x%x expected=0x%x\n",
              (unsigned long)address, actual, expected);
    return 1;
}

static int wait_state(int require_irq) {
    const uint64_t start = cycle_now();
    unsigned polls = 0U;
    for (;;) {
        /* 独立跨域CSR不是原子快照：先读IRQ，再读较新的STATUS。 */
        const uint32_t irq = csr_read(CSR_IRQ);
        const uint32_t status = csr_read(CSR_STATUS);
        const uint32_t fault = csr_read(CSR_FAULT);
        workload_trace.irq = irq;
        workload_trace.status = status;
        workload_trace.fault = fault;
        if ((status & STATUS_FAULT) || (fault & FAULT_VALID)) return 1;
        if ((status & (STATUS_VALID | STATUS_BUSY)) == STATUS_VALID &&
            (!require_irq || (irq & IRQ_LEVEL))) {
            workload_trace.wait_cycles = cycle_now() - start;
            return 0;
        }
        if ((++polls & 63U) == 0U) {
            workload_trace.wait_cycles = cycle_now() - start;
            if (workload_trace.wait_cycles >= WAIT_CYCLES) {
                LOG_ERROR("[FRONTEND][TIMEOUT] phase=%u cycles=%lu\n",
                          workload_trace.phase, (unsigned long)workload_trace.wait_cycles);
                return 1;
            }
        }
    }
}

int wait_window(void) { return wait_state(0); }
int wait_program(void) { return wait_state(1); }

int clear_completion(void) {
    const uint64_t start = cycle_now();
    csr_write(CSR_IRQ, IRQ_LEVEL);
    while ((csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U) {
        if (cycle_now() - start >= WAIT_CYCLES) return 1;
    }
    csr_write(CSR_IRQ, 0U);
    return 0;
}
