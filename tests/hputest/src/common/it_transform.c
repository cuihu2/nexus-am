#include <hpu/result.h>
#include <hpu/transform.h>
#include <hpu/report.h>

int transform_prepare(const uint32_t *image) {
    volatile uint32_t *memory = ddr_line(0U);
    if (image == NULL) return 1;
    printf("[HPU][TRANSFORM][PREPARE] window_lines=%u bytes=%u input=%u "
           "output=%u mod=%u factor=%u stage_base=%u\n",
           TRANSFORM_LINES, TRANSFORM_LINES * LINE_BYTES, TRANSFORM_INPUT,
           TRANSFORM_OUTPUT, TRANSFORM_MOD, TRANSFORM_FACTOR, TRANSFORM_STAGE_BASE);
    for (unsigned word = 0U; word < TRANSFORM_WORDS; ++word)
        memory[word] = image[word];
    clean_lines(0U, TRANSFORM_LINES);
    return 0;
}

int transform_check_memory(const uint32_t *image) {
    volatile const uint32_t *memory = ddr_line(0U);
    const unsigned first = TRANSFORM_OUTPUT * WORDS_PER_LINE;
    if (image == NULL) return 1;
    invalidate_lines(0U, TRANSFORM_OUTPUT);
    invalidate_lines(TRANSFORM_OUTPUT + TRANSFORM_N / WORDS_PER_LINE,
                     TRANSFORM_LINES - TRANSFORM_OUTPUT - TRANSFORM_N / WORDS_PER_LINE);
    for (unsigned word = 0U; word < TRANSFORM_WORDS; ++word) {
        if (word >= first && word < first + TRANSFORM_N) continue;
        uint32_t actual = memory[word];
        if (actual != image[word]) {
            printf("[HPU][TRANSFORM][FAIL] phase=readonly-guard word=%u "
                   "line=%u lane=%u address=0x%lx actual=0x%x expected=0x%x\n",
                   word, word / WORDS_PER_LINE, word % WORDS_PER_LINE,
                   (unsigned long)(MEM_BASE + word * sizeof(uint32_t)), actual, image[word]);
            return 1;
        }
    }
    printf("[HPU][TRANSFORM][MEMORY-PASS] readonly_guard_words=%u\n",
           TRANSFORM_WORDS - TRANSFORM_N);
    return 0;
}

int transform_check_result(const uint32_t *golden) {
    volatile const uint32_t *output = ddr_line(TRANSFORM_OUTPUT);
    if (golden == NULL) return 1;
    invalidate_lines(TRANSFORM_OUTPUT, TRANSFORM_N / WORDS_PER_LINE);
    return result_compare("transform-golden", output, golden, TRANSFORM_N, TRANSFORM_Q);
}

void transform_print_bindings(const struct transform_binding *bindings, unsigned count) {
    for (unsigned index = 0U; index < count; ++index) {
        printf("[HPU][TRANSFORM][DMA-PLAN] dma=%u instruction=%u op=%s "
               "line=%u count=%u artifact=%s\n", index, bindings[index].instruction,
               bindings[index].operation, bindings[index].line,
               bindings[index].lines, bindings[index].artifact);
    }
}
