#include <hpu/result.h>

/*
 * 测试点：IT-PERF-004
 * 目的：BConv 纯计算与端到端性能。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺 AM BConv 完整接收和相同输入/参数的 CPU 对照、纯计算计数边界与轮数。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_PERF_004\n");
    printf("缺 AM BConv 完整接收和相同输入/参数的 CPU 对照、纯计算计数边界与轮数。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
