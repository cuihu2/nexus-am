#include <hpu/arithmetic.h>
#include <hpu/cache.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/layout.h>
#include <hpu/result.h>
#include <hpu/sync.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_07_DLOAD_COMPUTE_DSTORE_PSYNC";
    volatile uint32_t *mod = hpu_line(HPU_LINE_MOD);
    volatile uint32_t *input_a = hpu_line(HPU_LINE_SRC_A);
    volatile uint32_t *input_b = hpu_line(HPU_LINE_SRC_B);
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    uint64_t mu = UINT64_MAX / HPU_MODULUS;
    unsigned i;
    unsigned timeout;

    hpu_test_begin(case_id,
                   "dload+padd+dstore+terminal-psync+self-check");

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

    /* 2. 准备 modulus、A、B；输出先 poison。 */
    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        mod[i] = 0U;
        input_a[i] = (uint32_t)(((uint64_t)i * 17U + 3U) % HPU_MODULUS);
        input_b[i] = (uint32_t)(((uint64_t)i * 29U + 5U) % HPU_MODULUS);
        output[i] = 0xdead0000U ^ i;
    }
    mod[0] = HPU_MODULUS;
    mod[1] = (uint32_t)mu;
    mod[2] = (uint32_t)(mu >> 32U);
    mod[3] = 0U;
    hpu_fence();
    hpu_cache_clean((uintptr_t)mod, HPU_LINE_BYTES);
    hpu_cache_clean((uintptr_t)input_a, HPU_LINE_BYTES);
    hpu_cache_clean((uintptr_t)input_b, HPU_LINE_BYTES);
    hpu_cache_clean((uintptr_t)output, HPU_LINE_BYTES);

    /* 3. 完整程序：mod/A/B DLOAD、PMODLD、PADD、DSTORE、PSYNC。 */
    hpu_dload_mod_p4(HPU_LINE_MOD, 1U);            /* 0x00b5292b */
    hpu_dload_p0(HPU_LINE_SRC_A, 1U);              /* 0x00b5102b */
    hpu_dload_p1(HPU_LINE_SRC_B, 1U);              /* 0x00b5122b */
    hpu_pmodld_0();                                 /* 0x6000000b */
    hpu_padd_p2_p0_p1();                            /* 0x0400400b */
    hpu_dstore_p2_release(HPU_LINE_OUTPUT, 1U);     /* 0x00b5542b */
    hpu_psync();                                    /* 0x7000000b */

    /* 4. 等待 terminal completion，检查 fault 并清 0x1c level。 */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        uint32_t status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) {
            return hpu_test_end(case_id, 31);
        }
        if ((hpu_csr_read(HPU_CSR_IRQ) & 1U) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return hpu_test_end(case_id, 32);
    hpu_csr_write(HPU_CSR_IRQ, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT / 16U; ++timeout) {
        if ((hpu_csr_read(HPU_CSR_IRQ) & 1U) == 0U) break;
    }
    hpu_csr_write(HPU_CSR_IRQ, 0U);
    if (timeout == HPU_TIMEOUT / 16U) return hpu_test_end(case_id, 33);

    /* 5. invalidate 后逐 word 计算模加 golden。 */
    hpu_cache_invalidate((uintptr_t)output, HPU_LINE_BYTES);
    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        uint32_t expected = (uint32_t)
            (((uint64_t)input_a[i] + input_b[i]) % HPU_MODULUS);
        if (output[i] != expected) {
            printf("HPU_SMOKE_MISMATCH word=%u actual=0x%x expected=0x%x\n",
                   i, output[i], expected);
            return hpu_test_end(case_id, 52);
        }
    }
    return hpu_test_end(case_id, 0);
}
