#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_PERF_006",
    "IT-PERF-006",
    "密文乘法算子加速比",
    "定向性能测试",
    3,
    HPU_CASE_PERF_CIPHERTEXT_MUL,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT |
        HPU_REQ_PERF_BASELINE | HPU_REQ_PERF_THRESHOLD,
    0u);
