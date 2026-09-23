#ifndef HPU_HADD_CASE_H
#define HPU_HADD_CASE_H

#include <hadd_layout.h>
#include <hpu/steps.h>

enum {
    HADD_TOTAL_WORDS = HPU_HADD_TOTAL_LINES * WORDS_PER_LINE,
    HADD_GOLDEN_WORDS = HPU_HADD_COMPONENTS * HPU_HADD_Q_COUNT * HPU_HADD_N
};

extern const uint32_t hadd_window[HADD_TOTAL_WORDS];
extern const uint32_t hadd_golden[HADD_GOLDEN_WORDS];
extern const uint32_t hadd_moduli[HPU_HADD_Q_COUNT];

int hadd_prepare(void);
int hadd_check_results(void);
int hadd_check_memory(void);

#endif
