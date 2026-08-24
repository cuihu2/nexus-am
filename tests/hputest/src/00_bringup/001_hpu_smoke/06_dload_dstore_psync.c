#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>
#include <hpu/sync.h>

/*
 * Purpose: close the DMA path with a software self-check:
 *
 *   DDR input A -> DLOAD p0 -> DSTORE p0 -> DDR output -> compare 4096 words
 *
 * The output is poisoned first, so a missing DSTORE cannot pass by observing
 * data that was already equal to the golden input.
 */
int main(void) {
    uint32_t status = 0U;
    unsigned timeout;

    if (hpu_fixture_validate_embedded() != 0) return 1;

    /* Program and read back all four shadow configuration CSRs. */
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

    /* Prepare producer input A at [0,64) and poison output at [128,192). */
    hpu_fixture_copy_to_ddr(HPU_LINE_SRC_A, hpu_rns_input_a);
    hpu_fixture_poison_output();

    /* DSTORE uses the 64-line length recorded by DLOAD for object p0. */
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);
    hpu_dstore_p0_release(HPU_LINE_OUTPUT, HPU_RNS_LINES);
    hpu_psync();

    /* Wait for terminal completion and reject any fault or lingering busy. */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) return 1;
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) != 0U)
            break;
    }
    if (timeout == HPU_TIMEOUT) return 1;
    if ((status & HPU_STATUS_BUSY) != 0U) return 1;

    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT / 16U; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT / 16U) return 1;

    /* Invalidate DDR output and compare every coefficient with immutable A. */
    if (hpu_fixture_check_dload_dstore() != 0) return 1;
    return 0;
}
