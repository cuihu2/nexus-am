#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_STR_004"
#define TESTPOINT "IT-STR-004"
#define DESCRIPTION "核内异常/分支预测与HPU指令交叉"
#define TEST_MODE "定向结构连接+功能覆盖"
#define PRIORITY 1
#define CASE_KIND "HPU_CASE_STR_CONTROL_FLOW"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_FUNCTION_COVERAGE"
#define SEED UINT32_C(0x5104)

int main(void) {
    /*
     * Fail closed: no trap-vector entry/handler, architecturally checked
     * exception return path, branch/mispredict placement, or retirement
     * monitor contract has been delivered for this case.  An untaken C branch
     * cannot stand in for that control-flow scenario; issue no HPU command.
     */
    (void)hpu_it_not_issued();
    return 1;
}
