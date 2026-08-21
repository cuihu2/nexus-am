#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_002",
    "IT-CMB-002",
    "完整NTT算子序列",
    "定向组合算子",
    1,
    HPU_CASE_CMB_NTT,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
