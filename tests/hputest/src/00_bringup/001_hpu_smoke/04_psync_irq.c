#include <am.h>
#include <hpu/csr.h>
#include <hpu/layout.h>
#include <hpu/result.h>
#include <hpu/sync.h>
#include <xsextra.h>

#include <stddef.h>
#include <stdint.h>

#define HPU_PLIC_SOURCE    257U
#define HPU_PLIC_CONTEXT_S 1U

static volatile uint32_t irq_count;
static volatile uint32_t irq_error;
static volatile uint32_t last_claim;

static int clear_hpu_irq_level(void) {
    unsigned timeout;

    hpu_csr_write(HPU_CSR_IRQ, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT / 16U; ++timeout) {
        if ((hpu_csr_read(HPU_CSR_IRQ) & 1U) == 0U) break;
    }
    hpu_csr_write(HPU_CSR_IRQ, 0U);
    return timeout == HPU_TIMEOUT / 16U ? 44 : 0;
}

static _Context *hpu_irq_handler(_Event event, _Context *context) {
    uint32_t claim;

    if (event.event != _EVENT_IRQ_IODEV) {
        irq_error = 41U;
        return context;
    }

    claim = plic_get_claim(HPU_PLIC_CONTEXT_S);
    last_claim = claim;
    if (claim != HPU_PLIC_SOURCE) {
        irq_error = 42U;
        if (claim != 0U) plic_clear_claim(HPU_PLIC_CONTEXT_S, claim);
        return context;
    }
    if ((hpu_csr_read(HPU_CSR_IRQ) & 1U) == 0U) {
        irq_error = 43U;
    } else if (clear_hpu_irq_level() != 0) {
        irq_error = 44U;
    }
    plic_clear_claim(HPU_PLIC_CONTEXT_S, claim);
    ++irq_count;
    return context;
}

int main(void) {
    const char *case_id = "HPU_SMOKE_001_04_PSYNC_IRQ";
    unsigned timeout;
    int rc = 0;

    hpu_test_begin(case_id, "psync-triggers-plic-interrupt");

    /* 1. 配置并提交 HPU 外存窗口。 */
    hpu_csr_write(HPU_CSR_FAULT, 1U);
    hpu_csr_write(HPU_CSR_IRQ, 1U);
    hpu_csr_write(HPU_CSR_IRQ, 0U);
    hpu_csr_write(HPU_CSR_BASE_LO, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write(HPU_CSR_BASE_HI, 0U);
    hpu_csr_write(HPU_CSR_SIZE_LO, HPU_WINDOW_LINES);
    hpu_csr_write(HPU_CSR_SIZE_HI, 0U);
    hpu_csr_write(HPU_CSR_COMMIT, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read(HPU_CSR_STATUS) & HPU_STATUS_WINDOW_VALID) != 0U)
            break;
    }
    if (timeout == HPU_TIMEOUT) return hpu_test_end(case_id, 11);

    /* 2. 配置 PLIC S-context 1 的 HPU source 257。 */
    _intr_write(0);
    if (_cte_init(NULL) != 0) return hpu_test_end(case_id, 47);
    seip_handler_reg(hpu_irq_handler);
    irq_count = 0U;
    irq_error = 0U;
    last_claim = 0U;
    plic_set_priority(HPU_PLIC_SOURCE, 1U);
    plic_set_threshold(HPU_PLIC_CONTEXT_S, 0U);
    plic_enable(HPU_PLIC_CONTEXT_S, HPU_PLIC_SOURCE);
    __asm__ volatile("csrs sie, %0"
                     : : "r"(UINT64_C(1) << 9U) : "memory");
    _intr_write(1);

    /* 3. 发 terminal PSYNC，等待中断 handler claim 257 并清 HPU level。 */
    hpu_psync(); /* 0x7000000b */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        uint32_t status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) {
            rc = 31;
            break;
        }
        if (irq_error != 0U) {
            rc = (int)irq_error;
            break;
        }
        if (irq_count == 1U) break;
        if (irq_count > 1U) {
            rc = 45;
            break;
        }
    }
    if (timeout == HPU_TIMEOUT) rc = 46;

    _intr_write(0);
    __asm__ volatile("csrc sie, %0"
                     : : "r"(UINT64_C(1) << 9U) : "memory");
    plic_disable(HPU_PLIC_CONTEXT_S, HPU_PLIC_SOURCE);
    plic_set_priority(HPU_PLIC_SOURCE, 0U);

    printf("HPU_SMOKE_IRQ count=%u claim=%u\n", irq_count, last_claim);
    return hpu_test_end(case_id, rc);
}
