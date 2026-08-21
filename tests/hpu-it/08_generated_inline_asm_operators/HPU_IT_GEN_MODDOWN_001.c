#include "../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_GEN_MODDOWN_001",
    "IT-GEN-MODDOWN-001",
    "inline-asm生成ModDown完整程序与golden",
    "生成算子交付",
    1,
    HPU_CASE_GENERATED_MODDOWN,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
