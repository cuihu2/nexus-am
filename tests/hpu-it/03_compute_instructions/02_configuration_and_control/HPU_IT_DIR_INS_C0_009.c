#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_INS_C0_009",
    "IT-INS-C0-009",
    "PFREE释放后同对象号复用",
    "定向对象生命周期",
    1,
    HPU_CASE_INS_PFREE,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
