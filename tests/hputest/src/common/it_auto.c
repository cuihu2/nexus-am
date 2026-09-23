#include <hpu/auto_case.h>
#include <hpu/log.h>
#include <hpu/report.h>

const uint32_t auto_moduli[AUTO_Q_COUNT] = {
    UINT32_C(50061313), UINT32_C(50077697),
    UINT32_C(50307073), UINT32_C(50552833)
};

int auto_prepare(void) {
    volatile uint32_t *memory = ddr_line(0U);

    LOG_DEBUG("[HPU][AUTO][PREPARE] N=%u Q=%u components=%u active=%u "
              "workspace=%u+%u output=%u guard=%u+%u\n",
              AUTO_N, AUTO_Q_COUNT, AUTO_COMPONENTS, HPU_AUTO_WINDOW_LINES,
              HPU_AUTO_WORKSPACE_OFFSET, HPU_AUTO_WORKSPACE_LINES,
              HPU_AUTO_OUTPUT_OFFSET, HPU_AUTO_GUARD_OFFSET,
              HPU_AUTO_GUARD_LINES);
    for (unsigned word = 0U; word < AUTO_TOTAL_WORDS; ++word)
        memory[word] = auto_window[word];
    clean_lines(0U, HPU_AUTO_TOTAL_LINES);
    return 0;
}

int auto_check_results(void) {
    int failed = 0;

    for (unsigned component = 0U; component < AUTO_COMPONENTS; ++component) {
        for (unsigned basis = 0U; basis < AUTO_Q_COUNT; ++basis) {
            const unsigned index = component * AUTO_Q_COUNT + basis;
            const unsigned line = HPU_AUTO_OUTPUT_OFFSET + index * AUTO_POLY_LINES;
            volatile const uint32_t *actual = ddr_line(line);
            const uint32_t *golden = auto_golden + index * AUTO_N;

            invalidate_lines(line, AUTO_POLY_LINES);
            LOG_DEBUG("[HPU][AUTO][OUTPUT] component=%u basis=%u q=%u line=%u "
                      "words=%u layout=coefficient-bit-reversed\n",
                      component, basis, auto_moduli[basis], line, AUTO_N);
            failed |= result_compare("auto-golden", actual, golden,
                                     AUTO_N, auto_moduli[basis]);
        }
    }
    return failed;
}

int auto_check_memory(void) {
    volatile const uint32_t *memory = ddr_line(0U);
    const unsigned writable_first = HPU_AUTO_WORKSPACE_OFFSET * WORDS_PER_LINE;
    const unsigned writable_end =
        (HPU_AUTO_WORKSPACE_OFFSET + HPU_AUTO_WORKSPACE_LINES) * WORDS_PER_LINE;

    invalidate_lines(0U, HPU_AUTO_WORKSPACE_OFFSET);
    invalidate_lines(HPU_AUTO_WORKSPACE_OFFSET + HPU_AUTO_WORKSPACE_LINES,
                     HPU_AUTO_GUARD_OFFSET -
                     (HPU_AUTO_WORKSPACE_OFFSET + HPU_AUTO_WORKSPACE_LINES));
    invalidate_lines(HPU_AUTO_GUARD_OFFSET, HPU_AUTO_GUARD_LINES);
    for (unsigned word = 0U; word < AUTO_TOTAL_WORDS; ++word) {
        if (word >= writable_first && word < writable_end) continue;
        if (memory[word] != auto_window[word]) {
            LOG_ERROR("[HPU][AUTO][FAIL] phase=readonly-guard word=%u line=%u "
                      "lane=%u address=0x%lx actual=0x%x expected=0x%x\n",
                      word, word / WORDS_PER_LINE, word % WORDS_PER_LINE,
                      (unsigned long)(MEM_BASE + (uintptr_t)word * sizeof(uint32_t)),
                      memory[word], auto_window[word]);
            return 1;
        }
    }
    LOG_DEBUG("[HPU][AUTO][MEMORY-PASS] readonly_guard_words=%u\n",
              AUTO_TOTAL_WORDS - (writable_end - writable_first));
    return 0;
}
