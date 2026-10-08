#include <hpu/edge.h>
#include <hpu/log.h>

#ifdef HPU_EDGE_HOST_TEST
extern uint64_t edge_host_cycle(void);
extern uint32_t edge_host_read(uintptr_t address);
#define EDGE_CYCLE() edge_host_cycle()
#define EDGE_READ(address) edge_host_read(address)
#else
static uint64_t edge_cycle(void) {
    uint64_t cycle;
    __asm__ volatile("rdcycle %0" : "=r"(cycle) : : "memory");
    return cycle;
}
#define EDGE_CYCLE() edge_cycle()
#define EDGE_READ(address) csr_read(address)
#endif

int edge_wait(uint64_t budget) {
    const uint64_t start = EDGE_CYCLE();
    if (budget == 0U) return 1;
    for (;;) {
        const uint32_t irq = EDGE_READ(CSR_IRQ);
        const uint32_t status = EDGE_READ(CSR_STATUS);
        const uint32_t fault = EDGE_READ(CSR_FAULT);
        const uint64_t elapsed = EDGE_CYCLE() - start;
        application_trace.irq = irq; application_trace.status = status;
        application_trace.fault = fault; application_trace.wait_cycles = elapsed;
        if ((fault & FAULT_VALID) || (status & STATUS_FAULT)) {
            LOG_ERROR("[HPU][EDGE][WAIT-FAIL] reason=fault irq=0x%x status=0x%x fault=0x%x\n", irq, status, fault);
            return 1;
        }
        /* 两个MMIO读不是原子快照，IRQ先到而BUSY未清时继续等待。 */
        if ((irq & IRQ_LEVEL) && (status & (STATUS_VALID | STATUS_BUSY)) == STATUS_VALID)
            return 0;
        if (elapsed >= budget) {
            LOG_ERROR("[HPU][EDGE][WAIT-FAIL] reason=software-budget cycles=%lu budget=%lu irq=0x%x status=0x%x\n",
                      (unsigned long)elapsed, (unsigned long)budget, irq, status);
            return 1;
        }
    }
}

int edge_failure(const char *file, unsigned line, const char *phase) {
    /* 现场仅供定位，不把三个独立寄存器读当成原子快照。 */
    uint32_t status = csr_read(CSR_STATUS), fault = csr_read(CSR_FAULT), irq = csr_read(CSR_IRQ);
    LOG_ERROR("[HPU][EDGE][FAIL] phase=%s status=0x%x fault=0x%x irq=0x%x\n", phase, status, fault, irq);
    return case_fail(file, line);
}
