#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_001",
    "IT-CMB-001",
    "BConv跨基转换完整闭环",
    "定向组合算子",
    1,
    HPU_CASE_CMB_BCONV,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
