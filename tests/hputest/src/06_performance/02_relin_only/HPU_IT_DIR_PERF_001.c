#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_PERF_001"
#define TESTPOINT "IT-PERF-001"
#define DESCRIPTION "RelinOnlycycle加速比验收"
#define TEST_MODE "定向性能测试"
#define PRIORITY 3
#define CASE_KIND "HPU_CASE_PERF_RELIN"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT | HPU_REQ_PERF_BASELINE | HPU_REQ_PERF_THRESHOLD"
#define SEED 0u

int main(void) {
    /*
     * 当前迁移边界 / current migration boundary:
     * 1. ELF 已嵌入 producer 的 A/B 两组 4096×uint32 输入，防止数据接口退化。
     * 2. 完整 RelinOnly 尚缺已解析的 DMA span/golden，性能计划也未冻结基线、重复次数和门限。
     * 3. 因此本用例不写 HPU CSR、不发指令、不输出伪 cycle 或伪 speedup。
     * 4. hpu_it_not_issued() 仅验证输入符号可链接；main 随后明确 return 1。
     * 5. 算法与性能契约补齐后，才实现同边界计时、逐系数比对和门限判定。
     */
    (void)hpu_it_not_issued();
    return 1;
}
