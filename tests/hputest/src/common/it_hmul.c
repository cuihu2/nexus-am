#include <hpu/hmul_case.h>
#include <hpu/log.h>
#include <hpu/report.h>

const uint32_t hmul_moduli[HPU_HMUL_Q_COUNT] = {
    HPU_HMUL_Q0, HPU_HMUL_Q1, HPU_HMUL_Q2, HPU_HMUL_Q3
};

int hmul_prepare(void) {
    volatile uint32_t *memory = ddr_line(0U);

    LOG_DEBUG("[HPU][HMUL][PREPARE] api=BfvOperationPlan::append_multiply "
              "N=%u Q=%u components=%u active=%u output=%u+%u guard=%u+%u\n",
              HPU_HMUL_N, HPU_HMUL_Q_COUNT, HPU_HMUL_COMPONENTS,
              HPU_HMUL_ACTIVE_LINES, HPU_HMUL_OUTPUT_OFFSET,
              HPU_HMUL_OUTPUT_LINES, HPU_HMUL_GUARD_OFFSET,
              HPU_HMUL_GUARD_LINES);
    for (unsigned word = 0U; word < HMUL_TOTAL_WORDS; ++word)
        memory[word] = hmul_window[word];
    clean_lines(0U, HPU_HMUL_TOTAL_LINES);
    return 0;
}

int hmul_check_results(void) {
    int failed = 0;

    for (unsigned component = 0U; component < HPU_HMUL_COMPONENTS; ++component) {
        for (unsigned basis = 0U; basis < HPU_HMUL_Q_COUNT; ++basis) {
            const unsigned index = component * HPU_HMUL_Q_COUNT + basis;
            const unsigned line =
                HPU_HMUL_OUTPUT_OFFSET + index * HPU_HMUL_POLY_LINES;
            volatile const uint32_t *actual = ddr_line(line);
            const uint32_t *golden = hmul_golden + index * HPU_HMUL_N;

            invalidate_lines(line, HPU_HMUL_POLY_LINES);
            LOG_DEBUG("[HPU][HMUL][OUTPUT] component=%u basis=%u q=%u "
                      "line=%u words=%u layout=coefficient-natural\n",
                      component, basis, hmul_moduli[basis], line, HPU_HMUL_N);
            failed |= result_compare("hmul-golden", actual, golden,
                                     HPU_HMUL_N, hmul_moduli[basis]);
        }
    }
    return failed;
}

int hmul_check_memory(void) {
    volatile const uint32_t *memory = ddr_line(0U);
    unsigned readonly_lines = 0U;

    invalidate_lines(0U, HPU_HMUL_TOTAL_LINES);
    for (unsigned line = 0U; line < HPU_HMUL_TOTAL_LINES; ++line) {
        if (hmul_writable_lines[line] != 0U) continue;
        ++readonly_lines;
        const unsigned first_word = line * WORDS_PER_LINE;
        for (unsigned lane = 0U; lane < WORDS_PER_LINE; ++lane) {
            const unsigned word = first_word + lane;
            if (memory[word] != hmul_window[word]) {
                LOG_ERROR("[HPU][HMUL][FAIL] phase=readonly-guard word=%u line=%u "
                          "lane=%u address=0x%lx actual=0x%x expected=0x%x\n",
                          word, line, lane,
                          (unsigned long)(MEM_BASE + (uintptr_t)word * sizeof(uint32_t)),
                          memory[word], hmul_window[word]);
                return 1;
            }
        }
    }
    LOG_DEBUG("[HPU][HMUL][MEMORY-PASS] readonly_guard_lines=%u\n", readonly_lines);
    return 0;
}
