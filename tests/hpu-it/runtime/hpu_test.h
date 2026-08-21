#ifndef HPU_IT_RUNTIME_H
#define HPU_IT_RUNTIME_H

#include <stdint.h>

typedef enum {
    HPU_CASE_CFG_CSR_ACCESS,
    HPU_CASE_CFG_COMMIT,
    HPU_CASE_CFG_WINDOW_ADDRESS,
    HPU_CASE_CFG_STATUS,
    HPU_CASE_CFG_FAULT,
    HPU_CASE_CFG_IRQ,
    HPU_CASE_PATH_CUSTOM0,
    HPU_CASE_PATH_CUSTOM1,
    HPU_CASE_PATH_LOOPBACK,
    HPU_CASE_PATH_STORE,
    HPU_CASE_PATH_MULTI_OBJECT,
    HPU_CASE_INS_PADD,
    HPU_CASE_INS_PSUB,
    HPU_CASE_INS_PMUL,
    HPU_CASE_INS_PMAC,
    HPU_CASE_INS_PNTT_STAGE,
    HPU_CASE_INS_PINTT_STAGE,
    HPU_CASE_INS_PMODLD,
    HPU_CASE_INS_PSYNC,
    HPU_CASE_INS_PFREE,
    HPU_CASE_CMB_BCONV,
    HPU_CASE_CMB_NTT,
    HPU_CASE_CMB_INTT,
    HPU_CASE_CMB_KEYSWITCH,
    HPU_CASE_CMB_NTT_AUTO,
    HPU_CASE_CMB_HADD,
    HPU_CASE_CMB_HMUL,
    HPU_CASE_CMB_ENCODE,
    HPU_CASE_CMB_RELINE,
    HPU_CASE_CMB_RESCALE,
    HPU_CASE_CMB_ROTATE,
    HPU_CASE_CMB_BOOTSTRAP,
    HPU_CASE_STING_PROGRAMS,
    HPU_CASE_STR_CHANNELS,
    HPU_CASE_STR_QUEUE,
    HPU_CASE_STR_QUEUE_MIXED,
    HPU_CASE_STR_MIXED,
    HPU_CASE_STR_CONTROL_FLOW,
    HPU_CASE_STR_RANDOM_MIXED,
    HPU_CASE_PERF_NTT,
    HPU_CASE_PERF_INTT,
    HPU_CASE_PERF_BCONV,
    HPU_CASE_PERF_KEYSWITCH,
    HPU_CASE_PERF_CIPHERTEXT_MUL,
    HPU_CASE_PERF_RELIN,
    HPU_CASE_APP_MINI_FHE,
    HPU_CASE_GENERATED_PMULT,
    HPU_CASE_GENERATED_CMULT,
    HPU_CASE_GENERATED_MODUP,
    HPU_CASE_GENERATED_MODDOWN
} hpu_case_kind;

enum {
    HPU_REQ_NONE              = 0,
    HPU_REQ_IT_MONITOR        = 1u << 0,
    HPU_REQ_READY_CONTROL     = 1u << 1,
    HPU_REQ_FAULT_INJECTION   = 1u << 2,
    HPU_REQ_IRQ_OBSERVATION   = 1u << 3,
    HPU_REQ_CACHE_CONTRACT    = 1u << 4,
    HPU_REQ_EXTERNAL_DATA     = 1u << 5,
    HPU_REQ_EXTERNAL_ENTRY    = 1u << 6,
    HPU_REQ_PERF_BASELINE     = 1u << 7,
    HPU_REQ_PERF_THRESHOLD    = 1u << 8,
    HPU_REQ_FUNCTION_COVERAGE = 1u << 9,
    HPU_REQ_STING             = 1u << 10,
    HPU_REQ_QUEUE_CONTROL     = 1u << 11,
    HPU_REQ_CAPACITY_CONTRACT = 1u << 12
};

typedef struct {
    const char *case_id;
    const char *testpoint_id;
    const char *title;
    const char *test_type;
    unsigned priority;
    hpu_case_kind kind;
    uint32_t requirements;
    uint32_t seed;
} hpu_testcase_descriptor;

extern const hpu_testcase_descriptor hpu_testcase;

int hpu_run_testcase(const hpu_testcase_descriptor *testcase);

#define HPU_DEFINE_TESTCASE(case_id_, testpoint_id_, title_, test_type_,       \
                            priority_, kind_, requirements_, seed_)             \
    const hpu_testcase_descriptor hpu_testcase = {                             \
        (case_id_), (testpoint_id_), (title_), (test_type_),                   \
        (priority_), (kind_), (requirements_), (seed_)                         \
    }

#endif
