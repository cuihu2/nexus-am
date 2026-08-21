#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_003",
    "IT-CMB-003",
    "完整INTT算子序列",
    "定向组合算子",
    1,
    HPU_CASE_CMB_INTT,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
