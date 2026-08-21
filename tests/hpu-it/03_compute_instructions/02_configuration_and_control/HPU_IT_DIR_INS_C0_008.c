#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_INS_C0_008",
    "IT-INS-C0-008",
    "PSYNC完成IRQ链路等待与清除",
    "定向同步指令",
    0,
    HPU_CASE_INS_PSYNC,
    HPU_REQ_IT_MONITOR | HPU_REQ_IRQ_OBSERVATION |
        HPU_REQ_CACHE_CONTRACT,
    0u);
