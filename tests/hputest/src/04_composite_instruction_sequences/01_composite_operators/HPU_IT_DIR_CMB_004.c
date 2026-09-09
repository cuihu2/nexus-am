#include <hpu/result.h>

/*
 * 测试点：IT-CMB-004
 * 目的：KeySwitch 全链。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺独立 KeySwitch 程序的已解析 DMA/常量/密钥/中间区绑定，不能借用其它算子的布局。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_004\n");
    printf("缺独立 KeySwitch 程序的已解析 DMA/常量/密钥/中间区绑定，不能借用其它算子的布局。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
