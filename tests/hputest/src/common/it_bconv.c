#include <hpu/bconv_case.h>
#include <hpu/result.h>

int bconv_prepare(void) {
    volatile uint32_t *memory = ddr_line(0U);
    printf("[HPU][BCONV][PREPARE] N=%u Q=%u P=%u window_lines=%u "
           "normalized_line=%u output_line=%u mod_line=%u\n", BCONV_N,
           BCONV_Q_COUNT, BCONV_P_COUNT, BCONV_LINES, BCONV_NORMALIZED, BCONV_OUTPUT, BCONV_MOD);
    for (unsigned word = 0U; word < BCONV_WORDS; ++word)
        memory[word] = bconv_window[word];
    clean_lines(0U, BCONV_LINES);
    return 0;
}

int bconv_check_memory(void) {
    volatile const uint32_t *memory = ddr_line(0U);
    const unsigned writable_first = BCONV_NORMALIZED * WORDS_PER_LINE;
    const unsigned writable_end = BCONV_MOD * WORDS_PER_LINE;
    invalidate_lines(0U, BCONV_LINES);
    for (unsigned word = 0U; word < BCONV_WORDS; ++word) {
        /* 只有 normalized Q 和 output P 两个 producer scratch/output span 可写。 */
        if (word >= writable_first && word < writable_end) continue;
        uint32_t actual = memory[word];
        if (actual != bconv_window[word]) {
            printf("[HPU][BCONV][FAIL] phase=readonly-guard word=%u line=%u lane=%u "
                   "actual=0x%x expected=0x%x\n", word, word / WORDS_PER_LINE,
                   word % WORDS_PER_LINE, actual, bconv_window[word]);
            return 1;
        }
    }
    printf("[HPU][BCONV][MEMORY-PASS] readonly_guard_words=%u\n",
           BCONV_WORDS - (writable_end - writable_first));
    return 0;
}

int bconv_check_results(const uint32_t *moduli) {
    if (moduli == NULL) return 1;
    /* 中间 normalized Q 用构建时独立 Python 数学式派生，最终 P 用 producer golden。 */
    for (unsigned basis = 0U; basis < BCONV_Q_COUNT + BCONV_P_COUNT; ++basis) {
        const unsigned output_basis = basis < BCONV_Q_COUNT ? basis : basis - BCONV_Q_COUNT;
        const unsigned line = (basis < BCONV_Q_COUNT ? BCONV_NORMALIZED : BCONV_OUTPUT) + output_basis * 64U;
        const uint32_t *golden = (basis < BCONV_Q_COUNT ? bconv_normalized : bconv_golden) + output_basis * BCONV_N;
        const char *phase = basis < BCONV_Q_COUNT ? "normalized-Q" : "producer-golden-P";
        volatile const uint32_t *actual_words = ddr_line(line);
        invalidate_lines(line, 64U);
        for (unsigned word = 0U; word < BCONV_N; ++word) {
            uint32_t actual = actual_words[word];
            if (actual != golden[word] || actual >= moduli[basis]) {
                printf("[HPU][BCONV][FAIL] phase=%s basis=%u coefficient=%u line=%u "
                       "actual=%u expected=%u q=%u\n", phase, output_basis, word,
                       line + word / WORDS_PER_LINE, actual, golden[word], moduli[basis]);
                return 1;
            }
        }
        printf("[HPU][BCONV][BASIS-PASS] phase=%s basis=%u compared=%u q=%u\n",
               phase, output_basis, BCONV_N, moduli[basis]);
    }
    return 0;
}
