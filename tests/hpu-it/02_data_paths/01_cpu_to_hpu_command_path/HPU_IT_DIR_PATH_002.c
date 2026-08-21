#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_PATH_002",
    "IT-PATH-002",
    "custom1命令与line参数逐笔配对",
    "定向通路",
    0,
    HPU_CASE_PATH_CUSTOM1,
    HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL |
        HPU_REQ_CACHE_CONTRACT,
    0u);
