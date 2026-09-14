#ifndef HPU_RESULT_H
#define HPU_RESULT_H

#include <hpu/log.h>

static inline void case_start(const char *case_path) {
    (void)case_path;
    LOG_EVENT("[HPU][START] %s\n", case_path);
}

static inline int case_fail(const char *case_path, unsigned line) {
    (void)case_path;
    (void)line;
    LOG_ERROR("[HPU][FAIL] %s:%u rc=1\n", case_path, line);
    return 1;
}

static inline int case_pass(const char *case_path) {
    (void)case_path;
    LOG_EVENT("[HPU][PASS] %s rc=0\n", case_path);
    return 0;
}

static inline void case_not_qualified(const char *case_path) {
    (void)case_path;
    LOG_ERROR("[HPU][FAIL][NOT_QUALIFIED] %s rc=1\n", case_path);
}

static inline void case_expected_failure(const char *case_path) {
    (void)case_path;
    LOG_ERROR("[HPU][FAIL][EXPECTED_RETURN_PROBE] %s rc=1\n", case_path);
}

static inline void case_hold(const char *case_path) {
    (void)case_path;
    LOG_EVENT("[HPU][HOLD] %s DLOAD issued; inspect waveform\n", case_path);
}

#endif
