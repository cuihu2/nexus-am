#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-STR-004
 * 目的：核内异常/分支预测与HPU指令交叉。
 * 模式：定向结构连接+功能覆盖（P1）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_EXTERNAL_ENTRY
 *   - HPU_REQ_FUNCTION_COVERAGE
 */

int main(void) {
    case_start(__FILE__);
    /*
     * Fail closed: no trap-vector entry/handler, architecturally checked
     * exception return path, branch/mispredict placement, or retirement
     * monitor contract has been delivered for this case.  An untaken C branch
     * cannot stand in for that control-flow scenario; issue no HPU command.
     */
    (void)not_issued();
    case_not_qualified(__FILE__);
    return 1;
}
