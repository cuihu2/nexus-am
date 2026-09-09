#include <hpu/result.h>

/*
 * 测试点：IT-CMB-005
 * 目的：NTT 与 Auto 联合序列。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * producer 的 Auto 专用数据和已解析表已具备，AM 对旋转因子、对象和 scratch 的接收校验尚未实现。
 */
int main(void) {
    case_start(__FILE__);
    printf("[HPU][BLOCKED] HPU_IT_DIR_CMB_005\n");
    printf("producer 的 Auto 专用数据和已解析表已具备，AM 对旋转因子、对象和 scratch 的接收校验尚未实现。\n");
    printf("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
