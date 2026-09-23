#include <hpu/log.h>
#include <hpu/result.h>

/*
 * 测试点：IT-PERF-005
 * 目的：KeySwitch 纯计算与端到端性能。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * KeySwitch数据绑定已完成；缺功能运行验证、匹配CPU基线、纯计算边界与轮数。
 */
int main(void) {
    case_start(__FILE__);
    LOG_ERROR("[HPU][BLOCKED] HPU_IT_DIR_PERF_005\n");
    LOG_ERROR("KeySwitch数据绑定已完成；缺功能运行验证、CPU基线、纯计算边界与轮数。\n");
    LOG_ERROR("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
