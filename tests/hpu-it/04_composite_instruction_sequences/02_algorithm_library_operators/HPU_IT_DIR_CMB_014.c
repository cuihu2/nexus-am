#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_014",
    "IT-CMB-014",
    "冻结Galois元素3的rotate/Auto组合序列",
    "定向算法库算子",
    2,
    HPU_CASE_CMB_ROTATE,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
