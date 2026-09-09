#include <hpu/result.h>

/*
 * 测试点：IT-STR-004
 * 目的：异常中断与 HPU 指令队列协同验证。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺可重复的外部事件注入和队列/提交观测接口，不能通过扩大 timeout 代替。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_STR_004\n");
    printf("缺可重复的外部事件注入和队列/提交观测接口，不能通过扩大 timeout 代替。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
