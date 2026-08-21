#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_009",
    "IT-CMB-009",
    "Poseidon算法库HADD组合序列",
    "定向算法库算子",
    2,
    HPU_CASE_CMB_HADD,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
