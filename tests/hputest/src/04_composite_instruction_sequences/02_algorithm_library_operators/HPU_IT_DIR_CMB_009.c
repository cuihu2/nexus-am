#include <hpu/result.h>

/*
 * 测试点：IT-CMB-009
 * 目的：通过真实 HPU/Poseidon 算法库接口验证 HADD。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 未指定算法库仓库/版本/API/密文参数。逐系数 PADD 不是 HADD 接口验收；PADD 已由 INS_C0_001 验证。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_009\n");
    printf("未指定算法库仓库/版本/API/密文参数。逐系数 PADD 不是 HADD 接口验收；PADD 已由 INS_C0_001 验证。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
