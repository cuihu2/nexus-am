#include "it_internal.h"

int hpu_it_compare_binary_output(hpu_it_arith_op operation,
                                 unsigned output_line, unsigned lines) {
    volatile const uint32_t *left;
    volatile const uint32_t *right;
    volatile const uint32_t *output;
    unsigned word;

    if (lines == 0U || lines > HPU_IT_POLY_LINES ||
        output_line > HPU_IT_WINDOW_LINES - lines ||
        (operation != HPU_IT_ARITH_PADD &&
         operation != HPU_IT_ARITH_PSUB &&
         operation != HPU_IT_ARITH_PMUL))
        return HPU_IT_ERR_ARGUMENT;
    left = hpu_it_line(HPU_IT_LINE_SRC_A);
    right = hpu_it_line(HPU_IT_LINE_SRC_B);
    hpu_it_invalidate_lines(output_line, lines);
    output = hpu_it_line(output_line);
    for (word = 0U; word < lines * HPU_WORDS_PER_LINE; ++word) {
        uint32_t expected;
        if (operation == HPU_IT_ARITH_PADD) {
            expected = hpu_it_mod_add(left[word], right[word], HPU_IT_Q0);
        } else if (operation == HPU_IT_ARITH_PSUB) {
            expected = hpu_it_mod_sub(left[word], right[word], HPU_IT_Q0);
        } else {
            expected = hpu_it_mod_mul(left[word], right[word], HPU_IT_Q0);
        }
        if (output[word] != expected) return HPU_IT_ERR_COMPARE;
    }
    return HPU_IT_OK;
}

int hpu_it_prepare_accumulator(uint32_t seed, unsigned lines) {
    volatile uint32_t *accumulator = hpu_it_line(HPU_IT_LINE_SCRATCH);
    unsigned word;

    if (lines == 0U || lines > HPU_IT_POLY_LINES) {
        return HPU_IT_ERR_ARGUMENT;
    }
    for (word = 0U; word < lines * HPU_WORDS_PER_LINE; ++word) {
        accumulator[word] = (seed + word * 17U) % HPU_IT_Q0;
    }
    hpu_it_clean_lines(HPU_IT_LINE_SCRATCH, lines);
    return HPU_IT_OK;
}

int hpu_it_compare_pmac_output(unsigned output_line, unsigned lines) {
    volatile const uint32_t *left;
    volatile const uint32_t *right;
    volatile const uint32_t *accumulator;
    volatile const uint32_t *output;
    unsigned word;

    if (lines == 0U || lines > HPU_IT_POLY_LINES ||
        output_line > HPU_IT_WINDOW_LINES - lines) {
        return HPU_IT_ERR_ARGUMENT;
    }
    left = hpu_it_line(HPU_IT_LINE_SRC_A);
    right = hpu_it_line(HPU_IT_LINE_SRC_B);
    accumulator = hpu_it_line(HPU_IT_LINE_SCRATCH);
    hpu_it_invalidate_lines(output_line, lines);
    output = hpu_it_line(output_line);
    for (word = 0U; word < lines * HPU_WORDS_PER_LINE; ++word) {
        uint32_t expected = hpu_it_mod_add(
            accumulator[word],
            hpu_it_mod_mul(left[word], right[word], HPU_IT_Q0), HPU_IT_Q0);
        if (output[word] != expected) return HPU_IT_ERR_COMPARE;
    }
    return HPU_IT_OK;
}
