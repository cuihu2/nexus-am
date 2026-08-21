#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_STR_001",
    "IT-STR-001",
    "custom0与custom1分通道反压及CDC定向",
    "定向结构连接",
    1,
    HPU_CASE_STR_CHANNELS,
    HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL |
        HPU_REQ_CACHE_CONTRACT | HPU_REQ_FUNCTION_COVERAGE,
    0u);
