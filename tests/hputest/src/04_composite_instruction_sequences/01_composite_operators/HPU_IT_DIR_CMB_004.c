#include <hpu/log.h>
#include <hpu/result.h>

/*
 * 测试点：IT-CMB-004
 * 目的：KeySwitch 全链。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * AM importer 已完成数据和DMA绑定；尚缺fixture、构建绑定及完整运行时校验。
 */
int main(void) {
    case_start(__FILE__);
    LOG_ERROR("[HPU][BLOCKED] HPU_IT_DIR_CMB_004\n");
    LOG_ERROR("AM importer已完成；尚缺fixture、Makefile.case及输出/guard/FAULT/IRQ运行时校验。\n");
    LOG_ERROR("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
