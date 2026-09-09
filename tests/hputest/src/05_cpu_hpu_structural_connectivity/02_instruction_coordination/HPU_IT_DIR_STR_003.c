#include <hpu/result.h>

/*
 * 测试点：IT-STR-003
 * 目的：分支错误路径、异常与特权切换协同验证。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺项目异常/特权入口与提交 monitor，无法用普通顺序 C 承诺错误路径 HPU 命令无副作用。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_STR_003\n");
    printf("缺项目异常/特权入口与提交 monitor，无法用普通顺序 C 承诺错误路径 HPU 命令无副作用。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
