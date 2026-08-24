#ifndef HPU_RESULT_H
#define HPU_RESULT_H

#include <klib.h>

static inline void hpu_test_begin(const char *case_id,
                                  const char *operation) {
    printf("HPU_IT_BEGIN case=%s testpoint=HPU-SMOKE-001 priority=P0\n",
           case_id);
    printf("HPU_SMOKE_STEP case=%s operation=%s\n", case_id, operation);
}

static inline int hpu_test_end(const char *case_id, int rc) {
    printf("HPU_IT_END case=%s result=%s rc=%d requirements=0x0\n",
           case_id, rc == 0 ? "PASS" : "FAIL", rc);
    return rc;
}

static inline int hpu_test_fail(const char *case_id,
                                const char *stage,
                                unsigned detail) {
    printf("HPU_SMOKE_FAIL case=%s stage=%s detail=%u\n",
           case_id, stage, detail);
    return hpu_test_end(case_id, 1);
}

#endif
