#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_005",
    "IT-CMB-005",
    "NTT/INTT与x到x三次幂Auto-Galois KeySwitch联合序列",
    "定向组合算子",
    2,
    HPU_CASE_CMB_NTT_AUTO,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
