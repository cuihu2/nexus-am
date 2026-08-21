#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_STING_CMB_007",
    "IT-CMB-007",
    "HPU可执行代码段约束随机排列组合",
    "STING约束随机",
    3,
    HPU_CASE_STING_PROGRAMS,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT |
        HPU_REQ_EXTERNAL_DATA | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_STING,
    0x8FD40BF5u);
