#include <workload.h>
#include <hpu/log.h>

static void record_error(unsigned line, unsigned word, uint32_t actual, uint32_t expected) {
    if (workload_trace.errors == 0U) {
        workload_trace.first_line = line;
        workload_trace.first_word = word;
        workload_trace.actual = actual;
        workload_trace.expected = expected;
    }
    ++workload_trace.errors;
}

int check_results(void) {
    unsigned mismatches = 0U;
    /* 只做一次全窗cache失效，不逐输出、逐系数反复失效或printf。 */
    invalidate_lines(IMAGE_LINES);
    for (unsigned index = 0U; index < OUTPUT_COUNT; ++index) {
        const struct output_span *out = &outputs[index];
        volatile const uint32_t *actual = line_ptr(out->line);
        for (unsigned word = 0U; word < out->words; ++word) {
            const uint32_t value = actual[word];
            const uint32_t expected = golden_words[out->golden_word + word];
            if (value == expected && value < out->modulus) continue;
            record_error(out->line + word / LINE_WORDS, word % LINE_WORDS, value, expected);
            if (mismatches++ < 4U)
                LOG_ERROR("[FRONTEND][DATA] step=%u c=%u mod=%u word=%u "
                          "actual=0x%x golden=0x%x q=%u\n",
                          out->step, out->component, out->modulus_id, word,
                          value, expected, out->modulus);
        }
    }
    if (mismatches != 0U)
        LOG_ERROR("[FRONTEND][DATA] total-mismatches=%u (only first 4 printed)\n", mismatches);
    return mismatches != 0U;
}

int check_memory(void) {
    /* check_results已失效整窗；只读区、空隙和窗外guard都要保持初始值。 */
    for (unsigned line = 0U; line < IMAGE_LINES; ++line) {
        if (writable_lines[line]) continue;
        volatile const uint32_t *actual = line_ptr(line);
        for (unsigned word = 0U; word < LINE_WORDS; ++word) {
            const uint32_t expected = initial_image[line * LINE_WORDS + word];
            const uint32_t value = actual[word];
            if (value == expected) continue;
            record_error(line, word, value, expected);
            LOG_ERROR("[FRONTEND][GUARD] line=%u word=%u actual=0x%x expected=0x%x\n",
                      line, word, value, expected);
            return 1;
        }
    }
    return 0;
}
