#ifndef HPU_BCONV_CASE_H
#define HPU_BCONV_CASE_H

#include <hpu/steps.h>

/* 固定本批 producer 的 Q4→P3/N4096 布局，不冒充 P→Q 或其他规模。 */
enum {
    BCONV_N = 4096,
    BCONV_Q_COUNT = 4,
    BCONV_P_COUNT = 3,
    BCONV_LINES = 2048,
    BCONV_WORDS = BCONV_LINES * WORDS_PER_LINE,
    BCONV_NORMALIZED = 1280,
    BCONV_OUTPUT = 1536,
    BCONV_MOD = 1728
};

extern const uint32_t bconv_window[BCONV_WORDS];
extern const uint32_t bconv_golden[BCONV_N * BCONV_P_COUNT];
extern const uint32_t bconv_normalized[BCONV_N * BCONV_Q_COUNT];

/* 只准备/检查内存，不隐含配置或发指令。 */
int bconv_prepare(void);
int bconv_check_memory(void);
int bconv_check_results(const uint32_t *moduli);

#endif
