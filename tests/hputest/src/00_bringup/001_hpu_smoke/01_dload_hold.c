#include <hpu/cache.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/layout.h>
#include <hpu/result.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_01_DLOAD_HOLD";
    volatile uint32_t *input = hpu_line(HPU_LINE_SRC_A);
    unsigned i;
    unsigned timeout;

    hpu_test_begin(case_id, "dload+while(1)");

    /* 1. 清旧事件，逐项配置并提交 HPU 外存窗口。 */
    hpu_csr_write(HPU_CSR_FAULT, 1U);
    hpu_csr_write(HPU_CSR_IRQ, 1U);
    hpu_csr_write(HPU_CSR_IRQ, 0U);
    hpu_csr_write(HPU_CSR_BASE_LO, (uint32_t)HPU_MEM_BASE);
    hpu_csr_write(HPU_CSR_BASE_HI, 0U);
    hpu_csr_write(HPU_CSR_SIZE_LO, HPU_WINDOW_LINES);
    hpu_csr_write(HPU_CSR_SIZE_HI, 0U);

    if (hpu_csr_read(HPU_CSR_BASE_LO) != (uint32_t)HPU_MEM_BASE ||
        hpu_csr_read(HPU_CSR_SIZE_LO) != HPU_WINDOW_LINES) {
        return hpu_test_end(case_id, 11);
    }
    hpu_csr_write(HPU_CSR_COMMIT, 1U);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        uint32_t status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) {
            return hpu_test_end(case_id, 12);
        }
        if ((status & HPU_STATUS_WINDOW_VALID) != 0U) break;
    }
    if (timeout == HPU_TIMEOUT) return hpu_test_end(case_id, 13);

    /* 2. 准备 DDR line 4，并把 CPU cache 中的数据 clean 到共享内存。 */
    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        input[i] = (uint32_t)(i * 17U + 3U);
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)input, HPU_LINE_BYTES);

    /* 3. x10=4、x11=1，发出 DLOAD p0（0x00b5102b）。 */
    hpu_dload_p0(HPU_LINE_SRC_A, 1U);

    /* 4. 故意不退出，给 IT 仿真留出观察 DLOAD 波形的时间。 */
    for (;;) {
        __asm__ volatile("nop");
    }
}
