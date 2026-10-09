#ifndef FRONTEND_PLATFORM_H
#define FRONTEND_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define MEM_BASE UINT64_C(0x87000000)
#define LINE_BYTES 256U
#define LINE_WORDS 64U
#define CSR_BASE UINT64_C(0x08000000)
#define CSR_BASE_LO (CSR_BASE + 0x00U)
#define CSR_BASE_HI (CSR_BASE + 0x04U)
#define CSR_SIZE_LO (CSR_BASE + 0x08U)
#define CSR_SIZE_HI (CSR_BASE + 0x0cU)
#define CSR_COMMIT (CSR_BASE + 0x10U)
#define CSR_STATUS (CSR_BASE + 0x14U)
#define CSR_FAULT (CSR_BASE + 0x18U)
#define CSR_IRQ (CSR_BASE + 0x1cU)

enum { STATUS_VALID = 1U, STATUS_BUSY = 2U, STATUS_FAULT = 4U };
enum { FAULT_VALID = 1U, IRQ_LEVEL = 1U, COMMIT = 1U };

#ifdef WORKLOAD_HOST_TEST
uint32_t test_csr_read(uintptr_t address);
void test_csr_write(uintptr_t address, uint32_t value);
uint64_t test_cycle(void);
volatile uint32_t *test_line(unsigned line);
static inline uint32_t csr_read(uintptr_t address) { return test_csr_read(address); }
static inline void csr_write(uintptr_t address, uint32_t value) {
    test_csr_write(address, value);
}
static inline uint64_t cycle_now(void) { return test_cycle(); }
static inline volatile uint32_t *line_ptr(unsigned line) { return test_line(line); }
#else
static inline void mem_fence(void) {
    __asm__ volatile("fence iorw, iorw" : : : "memory");
}
static inline uint32_t csr_read(uintptr_t address) {
    uint32_t value = *(volatile uint32_t *)address;
    mem_fence();
    return value;
}
static inline void csr_write(uintptr_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
    mem_fence();
}
static inline uint64_t cycle_now(void) {
    uint64_t cycle;
    __asm__ volatile("rdcycle %0" : "=r"(cycle) : : "memory");
    return cycle;
}
static inline volatile uint32_t *line_ptr(unsigned line) {
    return (volatile uint32_t *)(uintptr_t)(MEM_BASE + (uintptr_t)line * LINE_BYTES);
}
#endif

void clean_lines(unsigned lines);
void invalidate_lines(unsigned lines);
int expect_csr(uintptr_t address, uint32_t expected);
int wait_window(void);
int wait_program(void);
int clear_completion(void);

#endif
