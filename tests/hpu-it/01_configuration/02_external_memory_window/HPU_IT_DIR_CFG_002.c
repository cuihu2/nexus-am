#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CFG_002",
    "IT-CFG-002",
    "窗口A/B配置原子切换",
    "定向配置",
    0,
    HPU_CASE_CFG_COMMIT,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
