#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>

/*
 * Purpose: issue one 4096-coefficient DLOAD and then keep the CPU alive so
 * the DLOAD address, length, AXI handshakes, busy state, and object state can
 * be inspected in the IT waveform.
 *
 * This is intentionally an observation testcase.  The successful path does
 * not return 0 and does not claim a self-check result.
 */
int main(void) {
    uint32_t status = 0U;
    unsigned timeout;

    /* The ELF must contain both complete 4096-word input vectors. */
    if (hpu_fixture_validate_embedded() != 0) return 1;

    /* Clear stale fault and completion state before changing configuration. */
    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* Program the four shadow CSRs with an explicit 64-KiB HPU window. */
    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
                    (uint32_t)(HPU_MEM_BASE >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, HPU_WINDOW_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);

    /* Readback checks the CPU-to-CSR path; COMMIT itself is a write pulse. */
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

    /* Copy only producer input A: line 0, 64 lines, 4096 coefficients. */
    hpu_fixture_copy_to_ddr(HPU_LINE_SRC_A, hpu_rns_input_a);

    /*
     * From cuihu2/inline-asm:
     *   dload x10, x11, p0, poly, regular-bank
     * x10=0 is the line offset and x11=64 is the line count.
     */
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);

    /* Intentional waveform hold: stop this testcase with a cycle limit. */
    for (;;) {
        __asm__ volatile("nop");
    }
}
