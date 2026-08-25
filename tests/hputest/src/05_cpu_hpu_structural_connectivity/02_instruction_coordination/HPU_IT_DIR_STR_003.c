#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_STR_003"
#define TESTPOINT "IT-STR-003"
#define DESCRIPTION "普通运算访存与HPU指令交叉保序"
#define TEST_MODE "定向结构连接+功能覆盖"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_STR_MIXED"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL | HPU_REQ_CACHE_CONTRACT | HPU_REQ_FUNCTION_COVERAGE"
#define SEED UINT32_C(0x5103)

int main(void) {
    /*
     * Fail closed: this receiver lacks a producer-defined mixed instruction
     * stream that fixes the exact ordinary load/store/arithmetic locations
     * between HPU commands, plus the retirement/ordering monitor contract and
     * memory golden.  Running a PADD before or after unrelated CPU work would
     * not prove IT-STR-003, so no HPU command is issued.
     */
    (void)hpu_it_not_issued();
    return 1;
}
