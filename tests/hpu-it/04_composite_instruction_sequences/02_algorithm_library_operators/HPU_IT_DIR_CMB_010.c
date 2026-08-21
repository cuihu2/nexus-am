#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_010",
    "IT-CMB-010",
    "Poseidon算法库HMUL组合序列",
    "定向算法库算子",
    2,
    HPU_CASE_CMB_HMUL,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
