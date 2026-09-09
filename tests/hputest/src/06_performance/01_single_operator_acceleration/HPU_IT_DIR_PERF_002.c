#include <hpu/result.h>

/*
 * 测试点：IT-PERF-002
 * 目的：整体 NTT 纯计算与端到端性能。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 功能实现见 CMB_002；性能仍缺统一纯计算计数入口/边界与确认的 CPU 基线、轮数。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_PERF_002\n");
    printf("功能实现见 CMB_002；性能仍缺统一纯计算计数入口/边界与确认的 CPU 基线、轮数。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
