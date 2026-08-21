#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_PERF_002",
    "IT-PERF-002",
    "NTT算子cycle加速比验收",
    "定向性能测试",
    3,
    HPU_CASE_PERF_NTT,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT |
        HPU_REQ_PERF_BASELINE | HPU_REQ_PERF_THRESHOLD,
    0u);
