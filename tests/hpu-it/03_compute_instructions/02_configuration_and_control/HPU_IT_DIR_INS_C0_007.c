#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_INS_C0_007",
    "IT-INS-C0-007",
    "PMODLD模上下文切换效果",
    "定向配置指令",
    0,
    HPU_CASE_INS_PMODLD,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
