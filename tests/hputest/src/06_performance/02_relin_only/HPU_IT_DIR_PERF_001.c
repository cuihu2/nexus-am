#include <hpu/result.h>

/*
 * 测试点：IT-PERF-001
 * 目的：RelinOnly 纯计算与端到端性能。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺匹配的 CPU/HPU RelinOnly 实现、评估数据、统一纯计算计时边界与重复轮数。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_PERF_001\n");
    printf("缺匹配的 CPU/HPU RelinOnly 实现、评估数据、统一纯计算计时边界与重复轮数。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
