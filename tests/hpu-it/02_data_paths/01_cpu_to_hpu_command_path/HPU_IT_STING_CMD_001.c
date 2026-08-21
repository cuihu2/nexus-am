#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_STING_CMD_001",
    "IT-PATH-001",
    "custom0合法命令间隔与反压约束随机",
    "STING约束随机",
    2,
    HPU_CASE_PATH_CUSTOM0,
    HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL |
        HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_STING,
    0xF83138C9u);
