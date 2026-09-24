#ifndef HPU_CKKS_CASE_H
#define HPU_CKKS_CASE_H

#include <ckks_layout.h>
#include <hpu/steps.h>

extern const uint32_t ckks_window[CKKS_LINES * WORDS_PER_LINE];
extern const uint32_t ckks_golden[CKKS_GOLDEN_WORDS];
extern const uint8_t ckks_writable[CKKS_LINES];
int ckks_prepare(void);
int ckks_check_results(void);
int ckks_check_memory(void);

#endif
