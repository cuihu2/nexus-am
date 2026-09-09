#include <hpu/result.h>

/*
 * 测试点：IT-STR-005
 * 目的：CPU/HPU 混合随机指令的提交一致性。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺 STING 入口、参考执行模型和错误路径/异常注入契约。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_STING_STR_005\n");
    printf("缺 STING 入口、参考执行模型和错误路径/异常注入契约。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
