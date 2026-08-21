#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_004",
    "IT-CMB-004",
    "KeySwitch完整序列",
    "定向组合算子",
    1,
    HPU_CASE_CMB_KEYSWITCH,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
