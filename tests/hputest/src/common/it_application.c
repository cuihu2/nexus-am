#include <hpu/application_case.h>
#include <hpu/log.h>
#include <hpu/report.h>

int application_prepare(void) {
    volatile uint32_t *memory = ddr_line(0U);
    for (unsigned word = 0; word < HPU_APPLICATION_LINES * WORDS_PER_LINE; ++word)
        memory[word] = application_window[word];
    clean_lines(0U, HPU_APPLICATION_LINES);
    return 0;
}

int application_check_results(void) {
    int failed = 0;
    for (unsigned index = 0; index < HPU_APPLICATION_GOLDEN_COUNT; ++index) {
        const struct hpu_application_output *output = &hpu_application_outputs[index];
        invalidate_lines(output->line, output->lines);
        LOG_DEBUG("[HPU][APP][OUTPUT] step=%u component=%u modulus=%u line=%u\n",
                  output->step, output->component, output->modulus_id, output->line);
        failed |= result_compare("application", ddr_line(output->line),
                                 application_golden + output->golden_word,
                                 output->padded_words, output->modulus);
    }
    return failed;
}

int application_check_memory(void) {
    invalidate_lines(0U, HPU_APPLICATION_LINES);
    for (unsigned line = 0; line < HPU_APPLICATION_LINES; ++line) {
        if (application_writable[line])
            continue;
        volatile const uint32_t *actual = ddr_line(line);
        for (unsigned lane = 0; lane < WORDS_PER_LINE; ++lane) {
            const uint32_t expected = application_window[line * WORDS_PER_LINE + lane];
            const uint32_t value = actual[lane];
            if (value != expected) {
                LOG_ERROR("[HPU][APP][FAIL] phase=readonly-guard line=%u lane=%u "
                          "actual=0x%x expected=0x%x\n",
                          line, lane, value, expected);
                return 1;
            }
        }
    }
    return 0;
}
