#include <hpu/hadd_case.h>
#include <hpu/log.h>
#include <hpu/report.h>

const uint32_t hadd_moduli[HPU_HADD_Q_COUNT] = {
    HPU_HADD_Q0, HPU_HADD_Q1, HPU_HADD_Q2, HPU_HADD_Q3
};

int hadd_prepare(void) {
    volatile uint32_t *memory = ddr_line(0U);

    LOG_DEBUG("[HPU][HADD][PREPARE] api=BfvOperationPlan::append_add "
              "N=%u Q=%u components=%u active=%u output=%u+%u guard=%u+%u\n",
              HPU_HADD_N, HPU_HADD_Q_COUNT, HPU_HADD_COMPONENTS,
              HPU_HADD_ACTIVE_LINES, HPU_HADD_OUTPUT_OFFSET,
              HPU_HADD_OUTPUT_LINES, HPU_HADD_GUARD_OFFSET,
              HPU_HADD_GUARD_LINES);
    for (unsigned word = 0U; word < HADD_TOTAL_WORDS; ++word)
        memory[word] = hadd_window[word];
    clean_lines(0U, HPU_HADD_TOTAL_LINES);
    return 0;
}

int hadd_check_results(void) {
    int failed = 0;

    for (unsigned component = 0U; component < HPU_HADD_COMPONENTS; ++component) {
        for (unsigned basis = 0U; basis < HPU_HADD_Q_COUNT; ++basis) {
            const unsigned index = component * HPU_HADD_Q_COUNT + basis;
            const unsigned line =
                HPU_HADD_OUTPUT_OFFSET + index * HPU_HADD_POLY_LINES;
            volatile const uint32_t *actual = ddr_line(line);
            const uint32_t *golden = hadd_golden + index * HPU_HADD_N;

            invalidate_lines(line, HPU_HADD_POLY_LINES);
            LOG_DEBUG("[HPU][HADD][OUTPUT] component=%u basis=%u q=%u "
                      "line=%u words=%u layout=coefficient-natural\n",
                      component, basis, hadd_moduli[basis], line, HPU_HADD_N);
            failed |= result_compare("hadd-golden", actual, golden,
                                     HPU_HADD_N, hadd_moduli[basis]);
        }
    }
    return failed;
}

int hadd_check_memory(void) {
    volatile const uint32_t *memory = ddr_line(0U);
    const unsigned writable_first = HPU_HADD_OUTPUT_OFFSET * WORDS_PER_LINE;
    const unsigned writable_end = HPU_HADD_ACTIVE_LINES * WORDS_PER_LINE;

    invalidate_lines(0U, HPU_HADD_OUTPUT_OFFSET);
    invalidate_lines(HPU_HADD_GUARD_OFFSET, HPU_HADD_GUARD_LINES);
    for (unsigned word = 0U; word < HADD_TOTAL_WORDS; ++word) {
        if (word >= writable_first && word < writable_end) continue;
        if (memory[word] != hadd_window[word]) {
            LOG_ERROR("[HPU][HADD][FAIL] phase=readonly-guard word=%u line=%u "
                      "lane=%u address=0x%lx actual=0x%x expected=0x%x\n",
                      word, word / WORDS_PER_LINE, word % WORDS_PER_LINE,
                      (unsigned long)(MEM_BASE + (uintptr_t)word * sizeof(uint32_t)),
                      memory[word], hadd_window[word]);
            return 1;
        }
    }
    LOG_DEBUG("[HPU][HADD][MEMORY-PASS] readonly_guard_words=%u\n",
              HADD_TOTAL_WORDS - (writable_end - writable_first));
    return 0;
}
