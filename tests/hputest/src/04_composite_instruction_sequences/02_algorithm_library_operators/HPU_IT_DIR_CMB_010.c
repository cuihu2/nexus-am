#include <hpu/result.h>

/*
 * 测试点：IT-CMB-010
 * 目的：通过算法库接口验证 HMUL。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺库版本、HMUL 入口、密文输入/评估数据、DDR 绑定和接口级 golden。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_010\n");
    printf("缺库版本、HMUL 入口、密文输入/评估数据、DDR 绑定和接口级 golden。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
