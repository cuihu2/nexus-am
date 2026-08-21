#include "../../runtime/hpu_test.h"

HPU_DEFINE_TESTCASE(
    "HPU_IT_DIR_CMB_015",
    "IT-CMB-015",
    "NOT_SUPPORTED：Q4/P3配置无Poseidon bootstrap stage table与评估数据",
    "外部算法契约阻塞（不计入HPU执行通过）",
    2,
    HPU_CASE_CMB_BOOTSTRAP,
    HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT |
        HPU_REQ_EXTERNAL_DATA | HPU_REQ_EXTERNAL_ENTRY,
    0u);
