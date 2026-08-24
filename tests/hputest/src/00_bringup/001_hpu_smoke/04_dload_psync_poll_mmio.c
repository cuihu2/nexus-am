#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>
#include <hpu/sync.h>

/*
 * Synchronization method 1: MMIO polling.
 *
 * Issue the same DLOAD -> terminal PSYNC sequence as testcase 05, but keep
 * CPU interrupts disabled.  Software polls the HPU MMIO registers until it
 * has observed real DMA activity, the HPU is idle again, and PSYNC has raised
 * the completion level.  The DSTORE payload self-check remains testcase 06.
 */
int main(void) {
    uint32_t status = 0U;
    unsigned timeout;
    int saw_busy = 0;

    if (hpu_fixture_validate_embedded() != 0) return 1;

    /* Program and read back the HPU memory-window shadow CSRs. */
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

    /* Load all 4096 input-A coefficients, then notify program completion. */
    hpu_fixture_copy_to_ddr(HPU_LINE_SRC_A, hpu_rns_input_a);
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);
    hpu_psync();

    /*
     * Poll MMIO only; no trap handler participates in this testcase.
     * Requiring busy=1 before accepting busy=0 prevents an initial idle read
     * from being mistaken for completion.  IRQ[0] proves terminal PSYNC was
     * consumed, rather than only observing the DLOAD DMA become idle.
     */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) return 1;
        if ((status & HPU_STATUS_BUSY) != 0U) saw_busy = 1;
        if (saw_busy && (status & HPU_STATUS_BUSY) == 0U &&
            (hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) != 0U)
            break;
    }
    if (timeout == HPU_TIMEOUT) return 1;
    if ((status & (HPU_STATUS_WINDOW_VALID | HPU_STATUS_BUSY |
                   HPU_STATUS_FAULT_VALID)) != HPU_STATUS_WINDOW_VALID)
        return 1;

    /* Acknowledge and verify that the completion level is actually clear. */
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT / 16U; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT / 16U) return 1;

    return 0;
}
