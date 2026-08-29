#include <hpu/encoding.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-INS-C0-003
 * 目的：PMUL对象与立即数模式闭环。
 * 模式：定向指令闭环（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

static uint32_t c_mod_mul(uint32_t left, uint32_t right, uint32_t modulus) {
    return (uint32_t)(((uint64_t)left * right) % modulus);
}

int main(void) {
    const uint32_t seed = UINT32_C(0xc003);
    volatile const uint32_t *input_a;
    volatile const uint32_t *immediate_output;
    uint32_t status;
    unsigned timeout;
    unsigned word;

    /* Data only: copy producer A/B, build q0, and poison both output regions. */
    if (prepare_data(seed) != 0) return 1;

    hpu_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write32(HPU_CSR_BASE_HI_ADDR,
        (uint32_t)(HPU_MEM_BASE >> 32U));
    hpu_csr_write32(HPU_CSR_SIZE_LO_ADDR, WINDOW_LINES);
    hpu_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_csr_read32(HPU_CSR_BASE_LO_ADDR) !=
        (uint32_t)HPU_MEM_BASE) return 1;
    if (hpu_csr_read32(HPU_CSR_BASE_HI_ADDR) !=
        (uint32_t)(HPU_MEM_BASE >> 32U)) return 1;
    if (hpu_csr_read32(HPU_CSR_SIZE_LO_ADDR) != WINDOW_LINES)
        return 1;
    if (hpu_csr_read32(HPU_CSR_SIZE_HI_ADDR) != 0U) return 1;
    hpu_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (wait_window(1) != 0) return 1;

    /* Object mode: p2=p0*p1.  All words are producer-generated encodings. */
    if (dload_mod(LINE_MOD, 1U) != 0) return 1;
    if (pmodld(0U) != 0) return 1;
    if (dload(P0, LINE_A, POLY_LINES) != 0)
        return 1;
    if (dload(P1, LINE_B, POLY_LINES) != 0)
        return 1;
    if (pmul() != 0) return 1;
    if (dstore_release(P2, LINE_OUT, POLY_LINES) != 0)
        return 1;

    /* Immediate mode: the producer word means p2=p0*7 modulo active q0. */
    __asm__ volatile(".word %0" : : "i"(HPU_INSN_PMUL_IMM7_P2_P0)
        : "memory");
    if (dstore_release(P2, LINE_OUT_B, POLY_LINES) != 0)
        return 1;

    psync();
    if (wait_irq() != 0) return 1;
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) == 0U)
            break;
    }
    hpu_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (timeout == HPU_TIMEOUT) return 1;
    status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U) return 1;
    if ((status & (HPU_STATUS_BUSY | HPU_STATUS_FAULT_VALID)) != 0U)
        return 1;
    if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U)
        return 1;

    /* Compare both 4096-coefficient results with independent C arithmetic. */
    if (check_pmul(LINE_OUT, POLY_LINES) != 0) return 1;
    invalidate_lines(LINE_OUT_B, POLY_LINES);
    input_a = ddr_line(LINE_A);
    immediate_output = ddr_line(LINE_OUT_B);
    for (word = 0U; word < POLY_WORDS; ++word) {
        if (immediate_output[word] !=
            c_mod_mul(input_a[word], 7U, MOD_Q0)) return 1;
    }
    return 0;
}
