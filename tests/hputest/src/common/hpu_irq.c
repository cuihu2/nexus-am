#include <am.h>
#include <hpu/csr.h>
#include <hpu/irq.h>
#include <hpu/layout.h>
#include <klib.h>
#include <xsextra.h>

#include <stddef.h>
#include <stdint.h>

#define PLIC_SOURCE         257U
#define PLIC_PRIORITY_INDEX (PLIC_SOURCE - 1U)
#define PLIC_CONTEXT_S      1U
#define SIE_SEIE            (UINT64_C(1) << 9U)

/* LinkNan device block中的PLIC窗口，不使用通用XS旧地址0x3c000000。 */
#define HPU_PLIC_BASE UINT64_C(0x04000000)

#define PLIC_PRIORITY_ADDR \
    (HPU_PLIC_BASE + UINT64_C(0x4) + \
     (uint64_t)PLIC_PRIORITY_INDEX * sizeof(uint32_t))
#define PLIC_ENABLE_ADDR \
    (HPU_PLIC_BASE + UINT64_C(0x2000) + \
     (uint64_t)PLIC_CONTEXT_S * UINT64_C(0x80) + \
     (uint64_t)(PLIC_SOURCE / 32U) * sizeof(uint32_t))
#define PLIC_THRESHOLD_ADDR \
    (HPU_PLIC_BASE + UINT64_C(0x200000) + \
     (uint64_t)PLIC_CONTEXT_S * UINT64_C(0x1000))
#define PLIC_CLAIM_ADDR \
    (HPU_PLIC_BASE + UINT64_C(0x200004) + \
     (uint64_t)PLIC_CONTEXT_S * UINT64_C(0x1000))

extern int g_config_disable_timer;

static uint32_t mmio_read32(uint64_t address) {
    return *(volatile uint32_t *)(uintptr_t)address;
}

static void mmio_write32(uint64_t address, uint32_t data) {
    *(volatile uint32_t *)(uintptr_t)address = data;
}

static uint32_t hpu_plic_claim(void) {
    return mmio_read32(PLIC_CLAIM_ADDR);
}

static void hpu_plic_complete(uint32_t claim) {
    mmio_write32(PLIC_CLAIM_ADDR, claim);
}

static void hpu_plic_enable(void) {
    uint32_t enable = mmio_read32(PLIC_ENABLE_ADDR);

    mmio_write32(PLIC_ENABLE_ADDR,
                 enable | (1U << (PLIC_SOURCE % 32U)));
}

static void hpu_plic_disable(void) {
    uint32_t enable = mmio_read32(PLIC_ENABLE_ADDR);

    mmio_write32(PLIC_ENABLE_ADDR,
                 enable & ~(1U << (PLIC_SOURCE % 32U)));
}

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

    claim = hpu_plic_claim();
    if (claim != PLIC_SOURCE) {
        irq_error = 1U;
        if (claim != 0U) hpu_plic_complete(claim);
        irq_done = 1U;
        return context;
    }

    if ((csr_read(CSR_IRQ) & IRQ_LEVEL) == 0U || clear_level() != 0) {
        irq_error = 1U;
    }
    hpu_plic_complete(claim);
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
    mmio_write32(PLIC_PRIORITY_ADDR, 1U);
    mmio_write32(PLIC_THRESHOLD_ADDR, 0U);
    hpu_plic_enable();
    __asm__ volatile("fence iorw, iorw" : : : "memory");

    /* 配置地址错误时在这里失败，不再等到irq_wait()超时。 */
    {
        uint32_t priority = mmio_read32(PLIC_PRIORITY_ADDR);
        uint32_t enable = mmio_read32(PLIC_ENABLE_ADDR);
        uint32_t threshold = mmio_read32(PLIC_THRESHOLD_ADDR);

        printf("[HPU][IRQ] plic=0x04000000 priority=0x%x enable=0x%x "
               "threshold=0x%x\n",
               priority, enable, threshold);
        if (priority != 1U ||
            (enable & (1U << (PLIC_SOURCE % 32U))) == 0U ||
            threshold != 0U) {
            printf("[HPU][IRQ][FAIL] PLIC setup readback failed\n");
            return 1;
        }
    }
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
    hpu_plic_disable();
    mmio_write32(PLIC_PRIORITY_ADDR, 0U);
}
