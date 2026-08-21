#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_012",
    "IT-CMB-012",
    "Poseidon算法库reline组合序列",
    "定向算法库算子",
    2,
    HPU_CASE_CMB_RELINE,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
