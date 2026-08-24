#include <am.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>
#include <hpu/sync.h>
#include <xsextra.h>

#include <stddef.h>
#include <stdint.h>

#define HPU_PLIC_SOURCE    257U
#define HPU_PLIC_CONTEXT_S 1U

/* Written by the interrupt handler and polled by main(). */
static volatile uint32_t sync_flag;
static volatile uint32_t irq_error;

static int clear_hpu_irq_level(void) {
    unsigned timeout;

    /* IRQ[0] is write-one-to-clear; write zero afterwards to deassert clear. */
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT / 16U; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    return timeout == HPU_TIMEOUT / 16U ? 1 : 0;
}

static _Context *hpu_irq_handler(_Event event, _Context *context) {
    uint32_t claim;

    if (event.event != _EVENT_IRQ_IODEV) {
        irq_error = 41U;
        sync_flag = 1U;
        return context;
    }

    claim = plic_get_claim(HPU_PLIC_CONTEXT_S);
    if (claim != HPU_PLIC_SOURCE) {
        irq_error = 42U;
        if (claim != 0U) plic_clear_claim(HPU_PLIC_CONTEXT_S, claim);
        sync_flag = 1U;
        return context;
    }
    if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U) {
        irq_error = 43U;
    } else if (clear_hpu_irq_level() != 0) {
        irq_error = 44U;
    }
    plic_clear_claim(HPU_PLIC_CONTEXT_S, claim);
    sync_flag = 1U;
    return context;
}

/*
 * Synchronization method 2: interrupt completion.
 *
 * Issue the same DLOAD -> terminal PSYNC sequence as testcase 04.  PSYNC must
 * reach PLIC S-context 1 as source 257.  The handler clears the HPU IRQ level,
 * completes the PLIC claim, and then sets volatile sync_flag.  After waking,
 * main() still verifies that the HPU is idle and fault-free.
 */
int main(void) {
    uint32_t status = 0U;
    unsigned timeout;
    int rc = 0;

    if (hpu_fixture_validate_embedded() != 0) return 1;

    /* Clear stale events, program the HPU window, and check CSR readback. */
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
                    (uint32_t)(HPU_MEM_BASE >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, HPU_WINDOW_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);

    if (hpu_csr_read32(HPU_CSR_BASE_LO_ADDR) != (uint32_t)HPU_MEM_BASE)
        return 1;
    if (hpu_csr_read32(HPU_CSR_BASE_HI_ADDR) !=
        (uint32_t)(HPU_MEM_BASE >> 32U))
        return 1;
    if (hpu_csr_read32(HPU_CSR_SIZE_LO_ADDR) != HPU_WINDOW_LINES)
        return 1;
    if (hpu_csr_read32(HPU_CSR_SIZE_HI_ADDR) != 0U) return 1;

    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) return 1;
        if ((status & HPU_STATUS_WINDOW_VALID) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return 1;
    if ((status & HPU_STATUS_BUSY) != 0U) return 1;
    if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        return 1;
    if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) != 0U)
        return 1;

    /* Configure PLIC S-context 1 for HPU source 257. */
    _intr_write(0);
    if (_cte_init(NULL) != 0) return 1;
    seip_handler_reg(hpu_irq_handler);
    sync_flag = 0U;
    irq_error = 0U;
    plic_set_priority(HPU_PLIC_SOURCE, 1U);
    plic_set_threshold(HPU_PLIC_CONTEXT_S, 0U);
    plic_enable(HPU_PLIC_CONTEXT_S, HPU_PLIC_SOURCE);
    __asm__ volatile("csrs sie, %0"
                     : : "r"(UINT64_C(1) << 9U) : "memory");
    _intr_write(1);

    /* Prepare 4096 coefficients, then issue the same work as testcase 04. */
    hpu_fixture_copy_to_ddr(HPU_LINE_SRC_A, hpu_rns_input_a);
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);
    hpu_psync();

    /* The handler is the only code allowed to set sync_flag. */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if (irq_error != 0U) {
            rc = (int)irq_error;
            break;
        }
        if (sync_flag == 1U) break;
    }
    if (timeout == HPU_TIMEOUT) rc = 46;

    /* Interrupt arrival alone is insufficient: HPU must now be idle. */
    status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & (HPU_STATUS_WINDOW_VALID | HPU_STATUS_BUSY |
                   HPU_STATUS_FAULT_VALID)) != HPU_STATUS_WINDOW_VALID)
        rc = 47;
    if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        rc = 48;
    if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) != 0U)
        rc = 49;

    _intr_write(0);
    __asm__ volatile("csrc sie, %0"
                     : : "r"(UINT64_C(1) << 9U) : "memory");
    plic_disable(HPU_PLIC_CONTEXT_S, HPU_PLIC_SOURCE);
    plic_set_priority(HPU_PLIC_SOURCE, 0U);

    if (rc != 0) return 1;
    return 0;
}
