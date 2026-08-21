#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CFG_006",
    "IT-CFG-006",
    "PSYNC完成状态等待、IRQ清除与重触发",
    "定向完成事件",
    0,
    HPU_CASE_CFG_IRQ,
    HPU_REQ_IT_MONITOR | HPU_REQ_IRQ_OBSERVATION |
        HPU_REQ_CACHE_CONTRACT,
    0u);
