#ifndef HPU_HMUL_CASE_H
#define HPU_HMUL_CASE_H

#include <hmul_layout.h>
#include <hpu/steps.h>

enum {
    HMUL_TOTAL_WORDS = HPU_HMUL_TOTAL_LINES * WORDS_PER_LINE,
    HMUL_GOLDEN_WORDS = HPU_HMUL_COMPONENTS * HPU_HMUL_Q_COUNT * HPU_HMUL_N
};

extern const uint32_t hmul_window[HMUL_TOTAL_WORDS];
extern const uint32_t hmul_golden[HMUL_GOLDEN_WORDS];
extern const uint8_t hmul_writable_lines[HPU_HMUL_TOTAL_LINES];
extern const uint32_t hmul_moduli[HPU_HMUL_Q_COUNT];

int hmul_prepare(void);
int hmul_check_results(void);
int hmul_check_memory(void);

#endif
