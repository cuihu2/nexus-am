#include <hpu/cache.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/layout.h>
#include <hpu/result.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_03_DLOAD_POLL_CSR";
    volatile uint32_t *input = hpu_line(HPU_LINE_SRC_A);
    unsigned i;
    unsigned timeout;
    int saw_busy = 0;

    hpu_test_begin(case_id, "dload+poll-status-busy");

    /* 1. 配置窗口并等待 STATUS.window_valid。 */
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

    /* 2. 准备并 clean DDR 输入。 */
    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        input[i] = (uint32_t)(i * 17U + 3U);
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)input, HPU_LINE_BYTES);

    /* 3. 发 DLOAD p0，然后用 STATUS[1] 轮询 HPU 是否完成。 */
    hpu_dload_p0(HPU_LINE_SRC_A, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        uint32_t status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) {
            return hpu_test_end(case_id, 21);
        }
        if ((status & HPU_STATUS_BUSY) != 0U) saw_busy = 1;
        if (saw_busy && (status & HPU_STATUS_BUSY) == 0U) {
            printf("HPU_SMOKE_CSR_POLL busy_seen=1 busy_done=1 polls=%u\n",
                   timeout + 1U);
            return hpu_test_end(case_id, 0);
        }
    }

    printf("HPU_SMOKE_CSR_POLL busy_seen=%d busy_done=0\n", saw_busy);
    return hpu_test_end(case_id, saw_busy ? 22 : 23);
}
