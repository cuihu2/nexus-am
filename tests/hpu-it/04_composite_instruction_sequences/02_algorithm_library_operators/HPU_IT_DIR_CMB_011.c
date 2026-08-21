#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_011",
    "IT-CMB-011",
    "Poseidon Encode完整内联HPU序列与NTT域golden",
    "定向算法库算子",
    2,
    HPU_CASE_CMB_ENCODE,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
