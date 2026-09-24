#include <hpu/rotate_case.h>
#include <hpu/log.h>
#include <hpu/report.h>

const uint32_t rotate_moduli[HPU_ROTATE_Q_COUNT] = {
    HPU_ROTATE_Q0, HPU_ROTATE_Q1, HPU_ROTATE_Q2, HPU_ROTATE_Q3
};

int rotate_prepare(void) {
    volatile uint32_t *memory = ddr_line(0U);

    LOG_DEBUG("[HPU][ROTATE][PREPARE] api=BfvOperationPlan::append_rotate_rows "
              "N=%u Q=%u components=%u steps=%d galois=%u active=%u "
              "output=%u+%u guard=%u+%u\n",
              HPU_ROTATE_N, HPU_ROTATE_Q_COUNT, HPU_ROTATE_COMPONENTS,
              HPU_ROTATE_STEPS, HPU_ROTATE_GALOIS_ELEMENT,
              HPU_ROTATE_ACTIVE_LINES, HPU_ROTATE_OUTPUT_OFFSET,
              HPU_ROTATE_OUTPUT_LINES, HPU_ROTATE_GUARD_OFFSET,
              HPU_ROTATE_GUARD_LINES);
    for (unsigned word = 0U; word < ROTATE_TOTAL_WORDS; ++word)
        memory[word] = rotate_window[word];
    clean_lines(0U, HPU_ROTATE_TOTAL_LINES);
    return 0;
}

int rotate_check_results(void) {
    int failed = 0;

    for (unsigned component = 0U; component < HPU_ROTATE_COMPONENTS; ++component) {
        for (unsigned basis = 0U; basis < HPU_ROTATE_Q_COUNT; ++basis) {
            const unsigned index = component * HPU_ROTATE_Q_COUNT + basis;
            const unsigned line =
                HPU_ROTATE_OUTPUT_OFFSET + index * HPU_ROTATE_POLY_LINES;
            volatile const uint32_t *actual = ddr_line(line);
            const uint32_t *golden = rotate_golden + index * HPU_ROTATE_N;

            invalidate_lines(line, HPU_ROTATE_POLY_LINES);
            LOG_DEBUG("[HPU][ROTATE][OUTPUT] component=%u basis=%u q=%u "
                      "line=%u words=%u layout=coefficient-natural\n",
                      component, basis, rotate_moduli[basis], line,
                      HPU_ROTATE_N);
            failed |= result_compare("rotate-golden", actual, golden,
                                     HPU_ROTATE_N, rotate_moduli[basis]);
        }
    }
    return failed;
}

int rotate_check_memory(void) {
    volatile const uint32_t *memory = ddr_line(0U);
    unsigned readonly_lines = 0U;

    invalidate_lines(0U, HPU_ROTATE_TOTAL_LINES);
    for (unsigned line = 0U; line < HPU_ROTATE_TOTAL_LINES; ++line) {
        if (rotate_writable[line] != 0U)
            continue;
        ++readonly_lines;
        for (unsigned lane = 0U; lane < WORDS_PER_LINE; ++lane) {
            const unsigned word = line * WORDS_PER_LINE + lane;
            if (memory[word] != rotate_window[word]) {
                LOG_ERROR("[HPU][ROTATE][FAIL] phase=readonly-guard word=%u "
                          "line=%u lane=%u address=0x%lx actual=0x%x expected=0x%x\n",
                          word, line, lane,
                          (unsigned long)(MEM_BASE +
                              (uintptr_t)word * sizeof(uint32_t)),
                          memory[word], rotate_window[word]);
                return 1;
            }
        }
    }
    LOG_DEBUG("[HPU][ROTATE][MEMORY-PASS] readonly_lines=%u readonly_words=%u\n",
              readonly_lines, readonly_lines * WORDS_PER_LINE);
    return 0;
}
