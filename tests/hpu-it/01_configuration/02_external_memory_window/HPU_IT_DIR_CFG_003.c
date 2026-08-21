#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CFG_003",
    "IT-CFG-003",
    "窗口内地址与传输长度换算",
    "定向配置",
    0,
    HPU_CASE_CFG_WINDOW_ADDRESS,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
