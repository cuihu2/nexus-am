#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_CFG_002"
#define TESTPOINT "IT-CFG-002"
#define DESCRIPTION "窗口A/B配置原子切换"
#define TEST_MODE "定向配置"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_CFG_COMMIT"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
#define SEED 0u

int main(void) {
    enum { WINDOW_B_OFFSET = 256U, WINDOW_B_LINES = 256U };
    uint32_t expected[HPU_WORDS_PER_LINE];
    uintptr_t window_b = HPU_IT_MEM_BASE +
                         (uintptr_t)WINDOW_B_OFFSET * HPU_LINE_BYTES;
    unsigned word;
    int rc;

    /* producer 的 A/B 各含 4096 个系数；这里只另备一行窗口切换 canary。 */
    if (hpu_it_prepare_data(SEED) != 0) return 1;
    hpu_it_fill_line(WINDOW_B_OFFSET, UINT32_C(0xc102b), HPU_IT_Q0);
    hpu_it_poison_line(WINDOW_B_OFFSET + 64U, UINT32_C(0xc1020));
    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word)
        expected[word] = hpu_it_line(WINDOW_B_OFFSET)[word];
    hpu_it_clean_lines(WINDOW_B_OFFSET, 1U);
    hpu_it_clean_lines(WINDOW_B_OFFSET + 64U, 1U);

    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_FAULT_ADDR, 0U,
                            HPU_FAULT_VALID) != 0)
        return 1;

    /* 先提交窗口 A（完整 512 lines），所有 shadow 值逐项读回。 */
    hpu_it_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)HPU_IT_MEM_BASE);
    hpu_it_csr_write32(HPU_CSR_BASE_HI_ADDR,
                       (uint32_t)(HPU_IT_MEM_BASE >> 32U));
    hpu_it_csr_write32(HPU_CSR_SIZE_LO_ADDR, HPU_IT_WINDOW_LINES);
    hpu_it_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_BASE_LO_ADDR,
                            (uint32_t)HPU_IT_MEM_BASE, UINT32_MAX) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_BASE_HI_ADDR,
                            (uint32_t)(HPU_IT_MEM_BASE >> 32U),
                            UINT32_C(0xff)) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_SIZE_LO_ADDR,
                            HPU_IT_WINDOW_LINES, UINT32_MAX) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
        return 1;
    hpu_it_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (hpu_it_wait_window_valid(1) != 0) return 1;

    /* 再提交窗口 B；相同相对 line 现在必须落到物理 line 256 之后。 */
    hpu_it_csr_write32(HPU_CSR_BASE_LO_ADDR, (uint32_t)window_b);
    hpu_it_csr_write32(HPU_CSR_BASE_HI_ADDR, (uint32_t)(window_b >> 32U));
    hpu_it_csr_write32(HPU_CSR_SIZE_LO_ADDR, WINDOW_B_LINES);
    hpu_it_csr_write32(HPU_CSR_SIZE_HI_ADDR, 0U);
    if (hpu_it_csr_expect32(HPU_CSR_BASE_LO_ADDR, (uint32_t)window_b,
                            UINT32_MAX) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_BASE_HI_ADDR,
                            (uint32_t)(window_b >> 32U),
                            UINT32_C(0xff)) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_SIZE_LO_ADDR, WINDOW_B_LINES,
                            UINT32_MAX) != 0 ||
        hpu_it_csr_expect32(HPU_CSR_SIZE_HI_ADDR, 0U, 1U) != 0)
        return 1;
    hpu_it_csr_write32(HPU_CSR_COMMIT_ADDR, HPU_COMMIT_REQUEST);
    if (hpu_it_wait_window_valid(1) != 0) return 1;

    /* producer DLOAD/DSTORE 使用 x10=line、x11=count；相对地址保持不变。 */
    rc = hpu_it_issue_dload(HPU_IT_P0, 0U, 1U, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dstore(HPU_IT_P0, 64U, 1U,
                             HPU_IT_STORE_RELEASE);
    if (rc != 0) return 1;
    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    if (hpu_it_check_final_status() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* CPU 绝对地址 line 320 的 64 个 word 必须等于窗口 B 的 line 0。 */
    if (hpu_it_compare_line(WINDOW_B_OFFSET + 64U, expected,
                            HPU_WORDS_PER_LINE) != 0)
        return 1;
    return 0;
}
