#include <hpu/result.h>

/*
 * 测试点：IT-CMB-011
 * 目的：按原测试表保留 encode 库接口测试。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺 encode 库接口版本、编码参数、精度和可独立校验的数据。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_011\n");
    printf("缺 encode 库接口版本、编码参数、精度和可独立校验的数据。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
