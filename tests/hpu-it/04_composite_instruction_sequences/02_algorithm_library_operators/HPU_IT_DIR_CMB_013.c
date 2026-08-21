#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_013",
    "IT-CMB-013",
    "Poseidon Rescale完整内联HPU序列与降层golden",
    "定向算法库算子",
    2,
    HPU_CASE_CMB_RESCALE,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
