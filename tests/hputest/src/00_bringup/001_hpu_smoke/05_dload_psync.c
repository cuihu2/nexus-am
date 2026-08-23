#include <hpu/cache.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/layout.h>
#include <hpu/result.h>
#include <hpu/sync.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_05_DLOAD_PSYNC";
    volatile uint32_t *input = hpu_line(HPU_LINE_SRC_A);
    unsigned i;
    unsigned timeout;

    hpu_test_begin(case_id, "dload+terminal-psync");

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

    /* 2. 准备输入，DLOAD p0，然后发唯一 terminal PSYNC。 */
    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        input[i] = (uint32_t)(i * 17U + 3U);
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)input, HPU_LINE_BYTES);
    hpu_dload_p0(HPU_LINE_SRC_A, 1U); /* 0x00b5102b */
    hpu_psync();                      /* 0x7000000b */

    /* 3. 轮询 0x1c completion level，而不是猜测固定延时。 */
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        uint32_t status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) {
            return hpu_test_end(case_id, 31);
        }
        if ((hpu_csr_read(HPU_CSR_IRQ) & 1U) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return hpu_test_end(case_id, 32);

    /* 4. W1C 清 completion level，并写 0 结束 clear pulse。 */
    hpu_csr_write(HPU_CSR_IRQ, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT / 16U; ++timeout) {
        if ((hpu_csr_read(HPU_CSR_IRQ) & 1U) == 0U) break;
    }
    hpu_csr_write(HPU_CSR_IRQ, 0U);
    if (timeout == HPU_TIMEOUT / 16U) return hpu_test_end(case_id, 33);

    return hpu_test_end(case_id, 0);
}
