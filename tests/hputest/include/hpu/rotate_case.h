#ifndef HPU_ROTATE_CASE_H
#define HPU_ROTATE_CASE_H

#include <rotate_layout.h>
#include <hpu/steps.h>

enum {
    ROTATE_TOTAL_WORDS = HPU_ROTATE_TOTAL_LINES * WORDS_PER_LINE,
    ROTATE_GOLDEN_WORDS =
        HPU_ROTATE_COMPONENTS * HPU_ROTATE_Q_COUNT * HPU_ROTATE_N
};

extern const uint32_t rotate_window[ROTATE_TOTAL_WORDS];
extern const uint32_t rotate_golden[ROTATE_GOLDEN_WORDS];
extern const uint8_t rotate_writable[HPU_ROTATE_TOTAL_LINES];
extern const uint32_t rotate_moduli[HPU_ROTATE_Q_COUNT];

int rotate_prepare(void);
int rotate_check_results(void);
int rotate_check_memory(void);

#endif
