#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_PATH_001",
    "IT-PATH-001",
    "custom0命令单次接收与反压保持",
    "定向通路",
    0,
    HPU_CASE_PATH_CUSTOM0,
    HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL |
        HPU_REQ_CACHE_CONTRACT,
    0u);
