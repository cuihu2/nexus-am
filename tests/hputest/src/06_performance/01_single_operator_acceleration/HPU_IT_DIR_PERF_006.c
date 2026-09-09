#include <hpu/result.h>

/*
 * 测试点：IT-PERF-006
 * 目的：密文乘法纯计算与端到端性能。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺项目算法库密文乘法接口/基线与纯计算计数边界，不能用单系数 PMUL 计时替代。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_PERF_006\n");
    printf("缺项目算法库密文乘法接口/基线与纯计算计数边界，不能用单系数 PMUL 计时替代。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
