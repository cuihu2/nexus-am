#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-STR-003
 * 目的：普通运算访存与HPU指令交叉保序。
 * 模式：定向结构连接+功能覆盖（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_FUNCTION_COVERAGE
 */

int main(void) {
    case_start(__FILE__);
    /*
     * Fail closed: this receiver lacks a producer-defined mixed instruction
     * stream that fixes the exact ordinary load/store/arithmetic locations
     * between HPU commands, plus the retirement/ordering monitor contract and
     * memory golden.  Running a PADD before or after unrelated CPU work would
     * not prove IT-STR-003, so no HPU command is issued.
     */
    (void)not_issued();
    case_not_qualified(__FILE__);
    return 1;
}
