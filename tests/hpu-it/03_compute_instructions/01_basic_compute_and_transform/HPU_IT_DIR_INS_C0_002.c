#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_INS_C0_002",
    "IT-INS-C0-002",
    "PSUB模减结果写回闭环",
    "定向指令闭环",
    0,
    HPU_CASE_INS_PSUB,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
