#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_STING_PATH_005",
    "IT-PATH-005",
    "多对象多DDR区域隔离与容量边界",
    "STING约束随机",
    1,
    HPU_CASE_PATH_MULTI_OBJECT,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT |
        HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_STING |
        HPU_REQ_CAPACITY_CONTRACT,
    0x62B896C4u);
