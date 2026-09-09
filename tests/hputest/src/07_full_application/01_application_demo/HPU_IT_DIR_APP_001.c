#include <hpu/result.h>

/*
 * 测试点：IT-APP-001
 * 目的：完整 FHE A*B+C 与向量规约应用。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * 需确认应用为真实密文/FHE 还是模多项式，并提供对应接口/密钥/输入/精度/golden；当前两项均未实现。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_APP_001\n");
    printf("需确认应用为真实密文/FHE 还是模多项式，并提供对应接口/密钥/输入/精度/golden；当前两项均未实现。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
