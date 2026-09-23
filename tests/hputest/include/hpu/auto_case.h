#ifndef HPU_AUTO_CASE_H
#define HPU_AUTO_CASE_H

#include <auto_layout.h>
#include <hpu/steps.h>

enum {
    AUTO_N = 4096,
    AUTO_Q_COUNT = 4,
    AUTO_COMPONENTS = 2,
    AUTO_POLY_LINES = AUTO_N / WORDS_PER_LINE,
    AUTO_TOTAL_WORDS = HPU_AUTO_TOTAL_LINES * WORDS_PER_LINE
};

extern const uint32_t auto_window[AUTO_TOTAL_WORDS];
extern const uint32_t auto_golden[AUTO_COMPONENTS * AUTO_Q_COUNT * AUTO_N];
extern const uint32_t auto_moduli[AUTO_Q_COUNT];

int auto_prepare(void);
int auto_check_results(void);
int auto_check_memory(void);

#endif
