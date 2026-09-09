#include <hpu/result.h>

/*
 * 测试点：IT-CMB-012
 * 目的：通过算法库接口验证 reline。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺 reline 库入口及独立密钥/常量/scratch 绑定；不能以 PMAC 代替。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_012\n");
    printf("缺 reline 库入口及独立密钥/常量/scratch 绑定；不能以 PMAC 代替。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
