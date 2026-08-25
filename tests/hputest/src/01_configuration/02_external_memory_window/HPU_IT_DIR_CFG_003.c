#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_CFG_003"
#define TESTPOINT "IT-CFG-003"
#define DESCRIPTION "窗口内地址与传输长度换算"
#define TEST_MODE "定向配置"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_CFG_WINDOW_ADDRESS"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
#define SEED 0u

int main(void) {
    enum { LAST_POLY_LINE = HPU_IT_WINDOW_LINES - HPU_IT_POLY_LINES };
    unsigned line;
    int rc;

    /* 两组输入均来自 inline-asm producer，A 用于本次 4096-word 回环。 */
    if (hpu_it_prepare_data(SEED) != 0) return 1;
    for (line = 0U; line < HPU_IT_POLY_LINES; ++line)
        hpu_it_poison_line(LAST_POLY_LINE + line,
                           UINT32_C(0xc1030000) ^ line);
    hpu_it_clean_lines(LAST_POLY_LINE, HPU_IT_POLY_LINES);

    hpu_it_csr_write32(HPU_CSR_FAULT_ADDR, HPU_FAULT_VALID);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* BASE/SIZE 单位分别为 byte 地址和 256-byte line 数。 */
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
    if (hpu_it_check_final_status() != 0) return 1;

    /* x10=0/448，x11=64：覆盖窗口首行到最后一个合法 64-line 区间。 */
    rc = hpu_it_issue_dload(HPU_IT_P0, HPU_IT_LINE_SRC_A,
                            HPU_IT_POLY_LINES, HPU_IT_DMA_POLY);
    if (rc != 0) return 1;
    rc = hpu_it_issue_dstore(HPU_IT_P0, LAST_POLY_LINE,
                             HPU_IT_POLY_LINES, HPU_IT_STORE_RELEASE);
    if (rc != 0) return 1;
    hpu_it_issue_psync();
    if (hpu_it_wait_irq_level() != 0) return 1;
    if (hpu_it_check_final_status() != 0) return 1;
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, HPU_IRQ_LEVEL);
    hpu_it_csr_write32(HPU_CSR_IRQ_ADDR, 0U);

    /* 逐个比较 64 lines × 64 words，即完整 4096 个 32-bit 系数。 */
    if (hpu_it_compare_regions(LAST_POLY_LINE, HPU_IT_LINE_SRC_A,
                               HPU_IT_POLY_LINES) != 0)
        return 1;
    return 0;
}
