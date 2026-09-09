#include <hpu/result.h>

/*
 * 测试点：IT-CMB-014
 * 目的：通过算法库接口验证 rotate。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺算法库 rotate 接口、旋转索引/密钥绑定和接口级 golden；裸 Auto 不等同库接口。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_014\n");
    printf("缺算法库 rotate 接口、旋转索引/密钥绑定和接口级 golden；裸 Auto 不等同库接口。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
