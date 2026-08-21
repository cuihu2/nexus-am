#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_STR_006",
    "IT-STR-006",
    "命令缓存满载期间普通RISC-V指令交叉保序",
    "定向结构连接+功能覆盖",
    0,
    HPU_CASE_STR_QUEUE_MIXED,
    HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL |
        HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_ENTRY |
        HPU_REQ_FUNCTION_COVERAGE | HPU_REQ_QUEUE_CONTROL,
    0u);
