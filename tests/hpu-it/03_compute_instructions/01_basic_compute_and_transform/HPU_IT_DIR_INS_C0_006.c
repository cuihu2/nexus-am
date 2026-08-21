#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_INS_C0_006",
    "IT-INS-C0-006",
    "PINTT单stage结果写回闭环",
    "定向单stage",
    0,
    HPU_CASE_INS_PINTT_STAGE,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT,
    0u);
