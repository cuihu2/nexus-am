#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>

/*
 * Purpose: configure the HPU, issue a 64-line DLOAD, then poll STATUS until
 * busy has been observed high and subsequently low.  main() returns 0 only
 * after that complete transition; a fault or timeout returns 1.
 */
int main(void) {
    uint32_t status = 0U;
    unsigned timeout;
    int saw_busy = 0;

    if (hpu_fixture_validate_embedded() != 0) return 1;

    /* Clear stale events and explicitly program every configuration CSR. */
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

    /* DLOAD p0 from producer input A at [line 0, line 64). */
    hpu_fixture_copy_to_ddr(HPU_LINE_SRC_A, hpu_rns_input_a);
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);

    /* Poll the real STATUS address; no UART message is used as evidence. */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) return 1;
        if ((status & HPU_STATUS_BUSY) != 0U) saw_busy = 1;
        if (saw_busy && (status & HPU_STATUS_BUSY) == 0U) return 0;
    }

    return 1;
}
