#include <hpu/steps.h>

/*
 * 测试点：IT-PERF-005
 * 目的：KeySwitch算子加速比。
 * 模式：定向性能测试（P3）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_PERF_BASELINE
 *   - HPU_REQ_PERF_THRESHOLD
 */

int main(void) {
    /*
     * 当前迁移边界 / current migration boundary:
     * 1. ELF 已嵌入 producer 的 A/B 两组 4096×uint32 输入，防止数据接口退化。
     * 2. 完整 KeySwitch 尚缺已解析的 DMA span/golden，性能计划也未冻结基线、重复次数和门限。
     * 3. 因此本用例不写 HPU CSR、不发指令、不输出伪 cycle 或伪 speedup。
     * 4. not_issued() 仅验证输入符号可链接；main 随后明确 return 1。
     * 5. 算法与性能契约补齐后，才实现同边界计时、逐系数比对和门限判定。
     */
    (void)not_issued();
    return 1;
}
