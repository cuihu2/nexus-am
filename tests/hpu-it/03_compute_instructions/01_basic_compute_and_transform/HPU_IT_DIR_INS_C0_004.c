#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_INS_C0_004",
    "IT-INS-C0-004",
    "PMAC有效累加初值闭环",
    "定向指令闭环",
    0,
    HPU_CASE_INS_PMAC,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
