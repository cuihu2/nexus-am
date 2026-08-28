#include <hpu/csr.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>
#include "mm.h"

/*
 * Purpose: close a complete 4096-coefficient HPU compute path:
 *
 *   load modulus/A/B -> select modulus -> PMUL -> DSTORE -> PSYNC -> compare
 *
 * The data, modulus context, instruction words and DMA register protocol all
 * come from one inline-asm generation run.  The C oracle independently
 * computes (A[i] * B[i]) mod q for every one of the 4096 output coefficients.
 * A simulator report of X/Z on an SRAM write is a four-state data-integrity
 * failure, not a RISC-V Z-extension message; this testcase returns 1 when
 * that corruption reaches the 4096-word comparison.
 */
int main(void) {
    static const hpu_dma_span_t mm_spans[HPU_PROGRAM_MM_DMA_COUNT] = {
        /* Must follow outputs/mm/dma_relocation_manifest.csv instruction order. */
        {HPU_LINE_MOD, 1U},
        {HPU_LINE_SRC_A, HPU_RNS_LINES},
        {HPU_LINE_SRC_B, HPU_RNS_LINES},
        {HPU_LINE_OUTPUT, HPU_RNS_LINES},
    };
    uint32_t status = 0U;
    unsigned timeout;
    int rc;

    if (hpu_fixture_validate_embedded() != 0) return 1;
    if (hpu_fixture_validate_mm_assets() != 0) return 1;

    /* Program and read back all four HPU memory-window shadow CSRs. */
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

    /* Copy producer-owned images from ELF rodata; never preload golden as output. */
    hpu_fixture_copy_mod_ctx_to_ddr();
    hpu_fixture_copy_to_ddr(HPU_LINE_SRC_A, hpu_rns_input_a);
    hpu_fixture_copy_to_ddr(HPU_LINE_SRC_B, hpu_rns_input_b);
    hpu_fixture_poison_output();

    /*
     * This producer-generated function loads each span into x10/x11 immediately
     * before its custom1 word.  Its reviewed sequence is:
     *
     *   DLOAD mod -> PMODLD 0 -> DLOAD A -> DLOAD B -> PMUL p0,p1,p2
     *   -> PFREE p1/p2 -> DSTORE p0 -> PFREE p3 -> terminal PSYNC
     *
     * The generated function emits the program's only terminal PSYNC; this
     * testcase must not issue a second one.
     */
    rc = hpu_program_mm(mm_spans, HPU_PROGRAM_MM_DMA_COUNT);
    if (rc != 0) return 1;

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

    /* Compare producer golden, HPU output and the independent C PMUL oracle. */
    if (hpu_fixture_check_pmul() != 0) return 1;
    return 0;
}
