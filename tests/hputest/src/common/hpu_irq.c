#include <am.h>
#include <hpu/csr.h>
#include <hpu/irq.h>
#include <hpu/layout.h>
#include <xsextra.h>

#include <stddef.h>
#include <stdint.h>

#define PLIC_SOURCE         257U
#define PLIC_PRIORITY_INDEX (PLIC_SOURCE - 1U)
#define PLIC_CONTEXT_S      1U
#define SIE_SEIE            (UINT64_C(1) << 9U)

extern int g_config_disable_timer;

/* 只能由中断处理函数写，主程序仅轮询。 */
static volatile uint32_t irq_done;
static volatile uint32_t irq_error;

static int clear_level(void) {
    unsigned timeout;

    csr_write(CSR_IRQ, IRQ_LEVEL);
    for (timeout = 0U; timeout < TIMEOUT / 16U; ++timeout) {
        if ((csr_read(CSR_IRQ) & IRQ_LEVEL) == 0U) break;
    }
    csr_write(CSR_IRQ, 0U);
    return timeout == TIMEOUT / 16U ? 1 : 0;
}

static _Context *handler(_Event event, _Context *context) {
    uint32_t claim;

    if (event.event != _EVENT_IRQ_IODEV) {
        irq_error = 1U;
        irq_done = 1U;
        return context;
    }

    claim = plic_get_claim(PLIC_CONTEXT_S);
    if (claim != PLIC_SOURCE) {
        irq_error = 1U;
        if (claim != 0U) plic_clear_claim(PLIC_CONTEXT_S, claim);
        irq_done = 1U;
        return context;
    }

    if ((csr_read(CSR_IRQ) & IRQ_LEVEL) == 0U || clear_level() != 0) {
        irq_error = 1U;
    }
    plic_clear_claim(PLIC_CONTEXT_S, claim);
    irq_done = 1U;
    return context;
}

int irq_open(void) {
    _intr_write(0);
    /* HPU中断用例只观察外部中断，禁止CTE同时打开定时器中断。 */
    g_config_disable_timer = 1;
    if (_cte_init(NULL) != 0) return 1;

    irq_done = 0U;
    irq_error = 0U;
    seip_handler_reg(handler);
    /* priority 接口使用从 0 开始的数组下标；enable/claim 使用 source ID。 */
    plic_set_priority(PLIC_PRIORITY_INDEX, 1U);
    plic_set_threshold(PLIC_CONTEXT_S, 0U);
    plic_enable(PLIC_CONTEXT_S, PLIC_SOURCE);
    __asm__ volatile("csrs sie, %0" : : "r"(SIE_SEIE) : "memory");
    _intr_write(1);
    return 0;
}

int irq_wait(void) {
    unsigned timeout;

    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        if (irq_error != 0U) return 1;
        if (irq_done == 1U) return 0;
    }
    return 1;
}

void irq_close(void) {
    _intr_write(0);
    __asm__ volatile("csrc sie, %0" : : "r"(SIE_SEIE) : "memory");
    plic_disable(PLIC_CONTEXT_S, PLIC_SOURCE);
    plic_set_priority(PLIC_PRIORITY_INDEX, 0U);
}
