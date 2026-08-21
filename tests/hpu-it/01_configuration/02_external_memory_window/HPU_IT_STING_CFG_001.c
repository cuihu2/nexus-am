#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_STING_CFG_001",
    "IT-CFG-002",
    "配置写序与COMMIT间隔约束随机",
    "STING约束随机",
    2,
    HPU_CASE_CFG_COMMIT,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT |
        HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_STING,
    0x04BEA183u);
