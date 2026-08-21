#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CFG_004",
    "IT-CFG-004",
    "STATUS软件可见状态迁移",
    "定向状态",
    0,
    HPU_CASE_CFG_STATUS,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
