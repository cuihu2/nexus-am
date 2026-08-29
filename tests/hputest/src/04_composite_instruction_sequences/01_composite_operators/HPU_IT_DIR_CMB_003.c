#include <hpu/steps.h>

/*
 * 测试点：IT-CMB-003
 * 目的：完整INTT算子序列。
 * 模式：定向组合算子（P1）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    /*
     * 当前迁移边界 / current migration boundary:
     * 1. ELF 已嵌入 producer 的 A/B 两组 4096×uint32 输入，防止数据接口退化。
     * 2. 完整算法仍缺少已解析的 DMA span 表、全部中间区布局和不可变 golden。
     * 3. 因此本用例不写 HPU CSR、不发明完整 INTT 指令序列，也不产生假 self-check PASS。
     * 4. not_issued() 仅验证输入符号可链接；main 随后明确 return 1。
     * 5. producer 契约补齐后，才在这里展开 CSR→数据→指令→PSYNC→逐系数比对步骤。
     */
    (void)not_issued();
    return 1;
}
