#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_INS_C0_003",
    "IT-INS-C0-003",
    "PMUL对象与立即数模式闭环",
    "定向指令闭环",
    0,
    HPU_CASE_INS_PMUL,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
