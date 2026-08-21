#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_APP_001",
    "IT-APP-001",
    "小型FHE密文乘法、重线性化与解密校验端到端演示",
    "定向应用测试",
    3,
    HPU_CASE_APP_MINI_FHE,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT | HPU_REQ_PERF_THRESHOLD,
    0u);
