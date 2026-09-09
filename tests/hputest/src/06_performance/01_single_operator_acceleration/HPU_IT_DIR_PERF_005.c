#include <hpu/result.h>

/*
 * 测试点：IT-PERF-005
 * 目的：KeySwitch 纯计算与端到端性能。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺完整 KeySwitch 数据绑定和匹配 CPU 基线，纯计算计数边界与轮数未确认。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_PERF_005\n");
    printf("缺完整 KeySwitch 数据绑定和匹配 CPU 基线，纯计算计数边界与轮数未确认。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
