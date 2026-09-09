#include <hpu/result.h>

/*
 * 测试点：IT-CMB-013
 * 目的：通过算法库接口验证 rescale。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺库接口版本、输入 level/scale、输出精度与 rescale golden 契约。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_013\n");
    printf("缺库接口版本、输入 level/scale、输出精度与 rescale golden 契约。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
