#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CFG_001",
    "IT-CFG-001",
    "HPU相关CSR全量访问、修改与读回",
    "定向配置",
    0,
    HPU_CASE_CFG_CSR_ACCESS,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
