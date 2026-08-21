#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_STR_002",
    "IT-STR-002",
    "8条命令缓存满载与背压恢复",
    "定向结构连接",
    0,
    HPU_CASE_STR_QUEUE,
    HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL |
        HPU_REQ_CACHE_CONTRACT | HPU_REQ_FUNCTION_COVERAGE |
        HPU_REQ_QUEUE_CONTROL,
    0u);
