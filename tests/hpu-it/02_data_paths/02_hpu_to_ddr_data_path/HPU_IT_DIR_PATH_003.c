#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_PATH_003",
    "IT-PATH-003",
    "DDR经DLOAD进入对象并回环验证",
    "定向数据通路",
    0,
    HPU_CASE_PATH_LOOPBACK,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
