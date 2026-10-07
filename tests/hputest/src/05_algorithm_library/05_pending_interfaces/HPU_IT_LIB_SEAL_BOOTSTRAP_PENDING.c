#include <hpu/log.h>
#include <hpu/result.h>

/*
 * 测试点：05算法库待接入，原表IT-CMB-015
 * 目的：按原测试表保留 bootstrapping 库接口测试。
 * 此项尚未完成接入；只打印实际缺口，不发指令、不伪造 golden 或 PASS。
 * modified-SEAL版本已固定；AM尚缺bootstrap完整指令链、参数、评估数据、golden及执行入口。
 */
int main(void) {
    case_start(__FILE__);
    LOG_ERROR("[HPU][BLOCKED] HPU_IT_LIB_SEAL_BOOTSTRAP_PENDING\n");
    LOG_ERROR("modified-SEAL版本已固定；AM尚缺bootstrap完整指令链、参数、评估数据、golden及执行入口。\n");
    LOG_ERROR("[HPU][ACTION] See docs/V2_COVERAGE.md; no HPU command issued.\n");
    case_not_qualified(__FILE__);
    return 1;
}
