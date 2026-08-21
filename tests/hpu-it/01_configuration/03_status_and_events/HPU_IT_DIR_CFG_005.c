#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CFG_005",
    "IT-CFG-005",
    "FAULT方向对象记录与W1C可观察",
    "定向故障可观察",
    2,
    HPU_CASE_CFG_FAULT,
    HPU_REQ_IT_MONITOR | HPU_REQ_FAULT_INJECTION |
        HPU_REQ_CACHE_CONTRACT,
    0u);
