#include <hpu/log.h>
#include <hpu/keyswitch_case.h>
#include <hpu/report.h>

const uint32_t keyswitch_moduli[KEYSWITCH_Q_COUNT] = {
    UINT32_C(50061313), UINT32_C(50077697),
    UINT32_C(50307073), UINT32_C(50552833)
};

int keyswitch_prepare(void) {
    volatile uint32_t *memory = ddr_line(0U);

    LOG_DEBUG("[HPU][KEYSWITCH][PREPARE] N=%u Q=%u components=%u "
           "active_lines=%u total_lines=%u scratch=%u output=%u guard=%u+%u\n",
           KEYSWITCH_N, KEYSWITCH_Q_COUNT, KEYSWITCH_COMPONENTS,
           KEYSWITCH_ACTIVE_LINES, KEYSWITCH_TOTAL_LINES,
           KEYSWITCH_SCRATCH_OFFSET, KEYSWITCH_OUTPUT_OFFSET,
           KEYSWITCH_GUARD_OFFSET, KEYSWITCH_GUARD_LINES);
    for (unsigned word = 0U; word < KEYSWITCH_TOTAL_WORDS; ++word)
        memory[word] = keyswitch_window[word];
    clean_lines(0U, KEYSWITCH_TOTAL_LINES);
    return 0;
}

int keyswitch_check_results(void) {
    int failed = 0;

    for (unsigned component = 0U; component < KEYSWITCH_COMPONENTS; ++component) {
        for (unsigned basis = 0U; basis < KEYSWITCH_Q_COUNT; ++basis) {
            const unsigned index = component * KEYSWITCH_Q_COUNT + basis;
            const unsigned line = KEYSWITCH_OUTPUT_OFFSET + index * KEYSWITCH_POLY_LINES;
            volatile const uint32_t *actual = ddr_line(line);
            const uint32_t *golden = keyswitch_golden + index * KEYSWITCH_N;

            invalidate_lines(line, KEYSWITCH_POLY_LINES);
            LOG_DEBUG("[HPU][KEYSWITCH][OUTPUT] component=%u basis=%u q=%u "
                   "line=%u words=%u layout=coefficient-bit-reversed\n",
                   component, basis, keyswitch_moduli[basis], line, KEYSWITCH_N);
            failed |= result_compare("keyswitch-golden", actual, golden,
                                     KEYSWITCH_N, keyswitch_moduli[basis]);
        }
    }
    return failed;
}

int keyswitch_check_memory(void) {
    volatile const uint32_t *memory = ddr_line(0U);
    const unsigned writable_first = KEYSWITCH_SCRATCH_OFFSET * WORDS_PER_LINE;
    const unsigned writable_end = KEYSWITCH_ACTIVE_LINES * WORDS_PER_LINE;

    /* scratch和最终output连续可写；输入、密钥、常量及尾部guard必须保持不变。 */
    invalidate_lines(0U, KEYSWITCH_SCRATCH_OFFSET);
    invalidate_lines(KEYSWITCH_GUARD_OFFSET, KEYSWITCH_GUARD_LINES);
    for (unsigned word = 0U; word < KEYSWITCH_TOTAL_WORDS; ++word) {
        if (word >= writable_first && word < writable_end) continue;
        const uint32_t actual = memory[word];
        if (actual != keyswitch_window[word]) {
            LOG_ERROR("[HPU][KEYSWITCH][FAIL] phase=readonly-guard word=%u "
                   "line=%u lane=%u address=0x%lx actual=0x%x expected=0x%x\n",
                   word, word / WORDS_PER_LINE, word % WORDS_PER_LINE,
                   (unsigned long)(MEM_BASE + (uintptr_t)word * sizeof(uint32_t)),
                   actual, keyswitch_window[word]);
            return 1;
        }
    }
    LOG_DEBUG("[HPU][KEYSWITCH][MEMORY-PASS] readonly_guard_words=%u\n",
           KEYSWITCH_TOTAL_WORDS - (writable_end - writable_first));
    return 0;
}
