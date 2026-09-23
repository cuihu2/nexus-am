#ifndef HPU_KEYSWITCH_CASE_H
#define HPU_KEYSWITCH_CASE_H

#include <hpu/steps.h>

/* 固定本批 producer 的 N4096/Q4/P3/D2 KeySwitch，不冒充其它参数组合。 */
enum {
    KEYSWITCH_N = 4096,
    KEYSWITCH_Q_COUNT = 4,
    KEYSWITCH_COMPONENTS = 2,
    KEYSWITCH_POLY_LINES = KEYSWITCH_N / WORDS_PER_LINE,
    KEYSWITCH_ACTIVE_LINES = 14657,
    KEYSWITCH_GUARD_OFFSET = 14657,
    KEYSWITCH_GUARD_LINES = 64,
    KEYSWITCH_TOTAL_LINES = 14721,
    KEYSWITCH_SCRATCH_OFFSET = 12353,
    KEYSWITCH_OUTPUT_OFFSET = 14145,
    KEYSWITCH_TOTAL_WORDS = KEYSWITCH_TOTAL_LINES * WORDS_PER_LINE
};

extern const uint32_t keyswitch_window[KEYSWITCH_TOTAL_WORDS];
extern const uint32_t keyswitch_golden[KEYSWITCH_COMPONENTS * KEYSWITCH_Q_COUNT * KEYSWITCH_N];
extern const uint32_t keyswitch_moduli[KEYSWITCH_Q_COUNT];

/* 只准备和检查DDR；不配置CSR、不发指令，也不隐藏完成同步。 */
int keyswitch_prepare(void);
int keyswitch_check_results(void);
int keyswitch_check_memory(void);

#endif
