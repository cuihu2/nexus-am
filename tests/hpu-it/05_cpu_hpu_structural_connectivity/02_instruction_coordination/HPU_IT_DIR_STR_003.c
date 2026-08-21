#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_STR_003",
    "IT-STR-003",
    "普通运算访存与HPU指令交叉保序",
    "定向结构连接+功能覆盖",
    0,
    HPU_CASE_STR_MIXED,
    HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL |
        HPU_REQ_CACHE_CONTRACT | HPU_REQ_FUNCTION_COVERAGE,
    0u);
