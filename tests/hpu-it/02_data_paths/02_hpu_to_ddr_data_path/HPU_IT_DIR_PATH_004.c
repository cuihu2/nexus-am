#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_PATH_004",
    "IT-PATH-004",
    "对象及计算结果经DSTORE写回",
    "定向数据通路",
    0,
    HPU_CASE_PATH_STORE,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
