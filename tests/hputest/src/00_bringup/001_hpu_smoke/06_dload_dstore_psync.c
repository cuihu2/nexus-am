#include <hpu/cache.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/layout.h>
#include <hpu/result.h>
#include <hpu/sync.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_06_DLOAD_DSTORE_PSYNC";
    volatile uint32_t *input = hpu_line(HPU_LINE_SRC_A);
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    unsigned i;
    unsigned timeout;

    hpu_test_begin(case_id, "dload+dstore+terminal-psync+self-check");

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

    /* 2. 输入写确定数据；输出先写 poison，避免未写回也误判通过。 */
    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        input[i] = (uint32_t)(i * 17U + 3U);
        output[i] = 0xdead0000U ^ i;
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)input, HPU_LINE_BYTES);
    hpu_cache_clean((uintptr_t)output, HPU_LINE_BYTES);

    /* 3. DLOAD p0 -> DSTORE p0 release -> 唯一 terminal PSYNC。 */
    hpu_dload_p0(HPU_LINE_SRC_A, 1U);             /* 0x00b5102b */
    hpu_dstore_p0_release(HPU_LINE_OUTPUT, 1U);   /* 0x00b5502b */
    hpu_psync();                                  /* 0x7000000b */

    /* 4. 等 completion level，期间持续检查 STATUS.fault_valid。 */
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

    /* 5. invalidate 后逐 64 word self-check；main 返回码传出结果。 */
    hpu_cache_invalidate((uintptr_t)output, HPU_LINE_BYTES);
    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        if (output[i] != input[i]) {
            printf("HPU_SMOKE_MISMATCH word=%u actual=0x%x expected=0x%x\n",
                   i, output[i], input[i]);
            return hpu_test_end(case_id, 51);
        }
    }
    return hpu_test_end(case_id, 0);
}
