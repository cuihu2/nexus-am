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

/* 初始化/重新准备后由中断处理函数写，主程序仅轮询完成标志。 */
static volatile uint32_t irq_done;
static volatile uint32_t irq_error;

enum {
    IRQ_BAD_EVENT = 1U,
    IRQ_BAD_CLAIM,
    IRQ_MISSING_LEVEL,
    IRQ_CLEAR_TIMEOUT
};

/* ISR 内不调用 printf；只保存已经读到的现场，由 irq_wait() 在失败后输出。 */
static volatile uint32_t irq_handler_seen;
static volatile int irq_last_event;
static volatile uint32_t irq_last_claim;
static volatile uint32_t irq_last_level;
static volatile uint32_t irq_clear_last;
static volatile uint32_t irq_clear_polls;

static int clear_level(void) {
    unsigned timeout;
    uint32_t level = 0U;

    csr_write(CSR_IRQ, IRQ_LEVEL);
    for (timeout = 0U; timeout < TIMEOUT / 16U; ++timeout) {
        level = csr_read(CSR_IRQ);
        if ((level & IRQ_LEVEL) == 0U) break;
    }
    csr_write(CSR_IRQ, 0U);
    irq_clear_last = level;
    irq_clear_polls = timeout == TIMEOUT / 16U ? timeout : timeout + 1U;
    return timeout == TIMEOUT / 16U ? 1 : 0;
}

static _Context *handler(_Event event, _Context *context) {
    uint32_t claim;

    irq_handler_seen = 1U;
    irq_last_event = event.event;
    if (event.event != _EVENT_IRQ_IODEV) {
        irq_error = IRQ_BAD_EVENT;
        irq_done = 1U;
        return context;
    }

    claim = hpu_plic_claim();
    irq_last_claim = claim;
    if (claim != PLIC_SOURCE) {
        irq_error = IRQ_BAD_CLAIM;
        if (claim != 0U) hpu_plic_complete(claim);
        irq_done = 1U;
        return context;
    }

    irq_last_level = csr_read(CSR_IRQ);
    if ((irq_last_level & IRQ_LEVEL) == 0U) {
        irq_error = IRQ_MISSING_LEVEL;
    } else if (clear_level() != 0) {
        irq_error = IRQ_CLEAR_TIMEOUT;
    }
    hpu_plic_complete(claim);
    irq_done = 1U;
    return context;
}

int irq_open(void) {
    int rc;

    _intr_write(0);
    /* HPU中断用例只观察外部中断，禁止CTE同时打开定时器中断。 */
    g_config_disable_timer = 1;
    rc = _cte_init(NULL);
    if (rc != 0) {
        printf("[HPU][IRQ][FAIL] phase=open reason=cte-init rc=%d\n", rc);
        return 1;
    }

    irq_done = 0U;
    irq_error = 0U;
    irq_handler_seen = 0U;
    irq_last_event = 0;
    irq_last_claim = 0U;
    irq_last_level = 0U;
    irq_clear_last = 0U;
    irq_clear_polls = 0U;
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
        if (priority != 1U) {
            printf("[HPU][IRQ][FAIL] phase=open reason=plic-priority actual=0x%x expected=1 source=%u\n",
                   priority, PLIC_SOURCE);
            return 1;
        }
        if ((enable & (1U << (PLIC_SOURCE % 32U))) == 0U) {
            printf("[HPU][IRQ][FAIL] phase=open reason=plic-enable actual=0x%x required-mask=0x%x\n",
                   enable, 1U << (PLIC_SOURCE % 32U));
            return 1;
        }
        if (threshold != 0U) {
            printf("[HPU][IRQ][FAIL] phase=open reason=plic-threshold actual=0x%x expected=0\n",
                   threshold);
            return 1;
        }
    }
    __asm__ volatile("csrs sie, %0" : : "r"(SIE_SEIE) : "memory");
    _intr_write(1);
    return 0;
}

int irq_wait(void) {
    unsigned timeout;
    uint32_t error;
    const char *reason;

    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        error = irq_error;
        if (error != 0U) {
            switch (error) {
            case IRQ_BAD_EVENT: reason = "unexpected-event"; break;
            case IRQ_BAD_CLAIM: reason = "unexpected-claim"; break;
            case IRQ_MISSING_LEVEL: reason = "hpu-level-missing"; break;
            case IRQ_CLEAR_TIMEOUT: reason = "clear-level-timeout"; break;
            default: reason = "unknown-handler-error"; break;
            }
            printf("[HPU][IRQ][FAIL] phase=wait reason=%s error=%u polls=%u "
                   "event=%d claim=%u expected-claim=%u irq=0x%x clear-last=0x%x clear-polls=%u\n",
                   reason, error, timeout, irq_last_event, irq_last_claim,
                   PLIC_SOURCE, irq_last_level, irq_clear_last, irq_clear_polls);
            return 1;
        }
        if (irq_done == 1U) return 0;
    }
    /* 只打印缓存现场，不为诊断额外 claim PLIC 或读取 HPU MMIO。 */
    printf("[HPU][IRQ][FAIL] phase=wait reason=timeout polls=%u handler-seen=%u "
           "done=%u error=%u event=%d claim=%u irq=0x%x clear-last=0x%x clear-polls=%u\n",
           timeout, irq_handler_seen, irq_done, irq_error, irq_last_event,
           irq_last_claim, irq_last_level, irq_clear_last, irq_clear_polls);
    return 1;
}

int irq_rearm(void) {
    _intr_write(0);
    __asm__ volatile("fence iorw, iorw" : : : "memory");
    uint32_t status = csr_read(CSR_STATUS);
    uint32_t value = irq_done;
    /* 保持原先短路判断及 MMIO 读取顺序；每条失败日志只用已读到的值。 */
    if (value != 1U) {
        printf("[HPU][IRQ][FAIL] phase=rearm reason=not-done done=%u status=0x%x\n",
               value, status);
        return 1;
    }
    value = irq_error;
    if (value != 0U) {
        printf("[HPU][IRQ][FAIL] phase=rearm reason=handler-error error=%u status=0x%x\n",
               value, status);
        return 1;
    }
    value = csr_read(CSR_IRQ);
    if ((value & IRQ_LEVEL) != 0U) {
        printf("[HPU][IRQ][FAIL] phase=rearm reason=level-not-cleared irq=0x%x status=0x%x\n",
               value, status);
        return 1;
    }
    value = csr_read(CSR_FAULT);
    if ((value & FAULT_VALID) != 0U) {
        printf("[HPU][IRQ][FAIL] phase=rearm reason=fault-valid fault=0x%x status=0x%x\n",
               value, status);
        return 1;
    }
    if ((status & STATUS_VALID) == 0U) {
        printf("[HPU][IRQ][FAIL] phase=rearm reason=window-invalid status=0x%x\n", status);
        return 1;
    }
    if ((status & STATUS_BUSY) != 0U) {
        printf("[HPU][IRQ][FAIL] phase=rearm reason=busy status=0x%x\n", status);
        return 1;
    }
    if ((status & STATUS_FAULT) != 0U) {
        printf("[HPU][IRQ][FAIL] phase=rearm reason=status-fault status=0x%x\n", status);
        return 1;
    }
    irq_done = 0U;
    irq_handler_seen = 0U;
    irq_last_event = 0;
    irq_last_claim = 0U;
    irq_last_level = 0U;
    irq_clear_last = 0U;
    irq_clear_polls = 0U;
    __asm__ volatile("fence iorw, iorw" : : : "memory");
    _intr_write(1);
    return 0;
}

void irq_close(void) {
    _intr_write(0);
    __asm__ volatile("csrc sie, %0" : : "r"(SIE_SEIE) : "memory");
    hpu_plic_disable();
    mmio_write32(PLIC_PRIORITY_ADDR, 0U);
}
