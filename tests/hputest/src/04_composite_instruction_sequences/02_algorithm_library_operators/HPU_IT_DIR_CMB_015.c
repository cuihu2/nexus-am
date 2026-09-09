#include <hpu/result.h>

/*
 * 测试点：IT-CMB-015
 * 目的：按原测试表保留 bootstrapping 库接口测试。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 缺库版本、bootstrapping 完整输入/评估数据、精度与可执行入口。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_015\n");
    printf("缺库版本、bootstrapping 完整输入/评估数据、精度与可执行入口。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
