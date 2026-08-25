#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_STING_CFG_001"
#define TESTPOINT "IT-CFG-002"
#define DESCRIPTION "配置写序与COMMIT间隔约束随机"
#define TEST_MODE "STING约束随机"
#define PRIORITY 2
#define CASE_KIND "HPU_CASE_CFG_COMMIT"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_STING"
#define SEED 0x04BEA183u

int main(void) {
    static const unsigned offsets[] = {0U, 32U, 64U, 16U, 0U};
    unsigned index;

    /* 固定 seed 只负责 data-only fixture；STING 激励仍由外部入口提供。 */
    if (hpu_it_prepare_data(SEED) != 0) return 1;

    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_FAULT_ADDR, 0U,
                            HPU_FAULT_VALID) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_IRQ_ADDR, 0U, HPU_IRQ_LEVEL) != 0)
        return 1;

    /*
     * 偶数轮和奇数轮使用不同 shadow 写序；每轮 COMMIT 前都逐项读回。
     * 这只自检合法软件序列，随机间隔/反压覆盖率不能由 C 代码冒充。
     */
    for (index = 0U; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        uintptr_t base = HPU_IT_MEM_BASE +
                         (uintptr_t)offsets[index] * HPU_LINE_BYTES;
        uint32_t lines = HPU_IT_WINDOW_LINES - offsets[index];

        if ((index & 1U) == 0U) {
            hpu_it_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)base);
            hpu_it_csr_write32(HPU_CSR_BASE_HI_ADDR,
                               (uint32_t)(base >> 32U));
            hpu_it_csr_write32(HPU_CSR_SIZE_LO_ADDR, lines);
            hpu_it_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
        } else {
            hpu_it_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
            hpu_it_csr_write32(HPU_CSR_BASE_HI_ADDR,
                               (uint32_t)(base >> 32U));
            hpu_it_csr_write32(HPU_CSR_SIZE_LO_ADDR, lines);
            hpu_it_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)base);
        }
        if (hpu_it_csr_expect32(HPU_CSR_BASE_LO_ADDR, (uint32_t)base,
                                UINT32_MAX) != 0 ||
            hpu_it_csr_expect32(HPU_CSR_BASE_HI_ADDR,
                                (uint32_t)(base >> 32U),
                                UINT32_C(0xff)) != 0 ||
            hpu_it_csr_expect32(HPU_CSR_SIZE_LO_ADDR, lines,
                                UINT32_MAX) != 0 ||
            hpu_it_csr_expect32(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
            return 1;

        hpu_it_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
        if (hpu_it_wait_window_valid(1) != 0) return 1;
        if (hpu_it_check_final_status() != 0) return 1;
    }

    return 0;
}
