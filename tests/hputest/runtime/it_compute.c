#include "it_internal.h"

enum binary_operation {
    CHECK_PADD,
    CHECK_PSUB,
    CHECK_PMUL
};

static int check_binary(enum binary_operation operation,
                        unsigned output_line, unsigned lines) {
    volatile const uint32_t *left;
    volatile const uint32_t *right;
    volatile const uint32_t *output;
    unsigned word;

    if (lines == 0U || lines > POLY_LINES ||
        output_line > WINDOW_LINES - lines ||
        (operation != CHECK_PADD && operation != CHECK_PSUB &&
         operation != CHECK_PMUL))
        return STEP_ERR_ARGUMENT;
    left = ddr_line(LINE_A);
    right = ddr_line(LINE_B);
    invalidate_lines(output_line, lines);
    output = ddr_line(output_line);
    for (word = 0U; word < lines * HPU_WORDS_PER_LINE; ++word) {
        uint32_t expected;
        if (operation == CHECK_PADD) {
            expected = mod_add(left[word], right[word], MOD_Q0);
        } else if (operation == CHECK_PSUB) {
            expected = mod_sub(left[word], right[word], MOD_Q0);
        } else {
            expected = mod_mul(left[word], right[word], MOD_Q0);
        }
        if (output[word] != expected) return STEP_ERR_COMPARE;
    }
    return STEP_OK;
}

int check_padd(unsigned output_line, unsigned lines) {
    return check_binary(CHECK_PADD, output_line, lines);
}

int check_psub(unsigned output_line, unsigned lines) {
    return check_binary(CHECK_PSUB, output_line, lines);
}

int check_pmul_result(unsigned output_line, unsigned lines) {
    return check_binary(CHECK_PMUL, output_line, lines);
}

int prepare_accumulator(uint32_t seed, unsigned lines) {
    volatile uint32_t *accumulator = ddr_line(LINE_SCRATCH);
    unsigned word;

    if (lines == 0U || lines > POLY_LINES) {
        return STEP_ERR_ARGUMENT;
    }
    for (word = 0U; word < lines * HPU_WORDS_PER_LINE; ++word) {
        accumulator[word] = (seed + word * 17U) % MOD_Q0;
    }
    clean_lines(LINE_SCRATCH, lines);
    return STEP_OK;
}

int check_pmac(unsigned output_line, unsigned lines) {
    volatile const uint32_t *left;
    volatile const uint32_t *right;
    volatile const uint32_t *accumulator;
    volatile const uint32_t *output;
    unsigned word;

    if (lines == 0U || lines > POLY_LINES ||
        output_line > WINDOW_LINES - lines) {
        return STEP_ERR_ARGUMENT;
    }
    left = ddr_line(LINE_A);
    right = ddr_line(LINE_B);
    accumulator = ddr_line(LINE_SCRATCH);
    invalidate_lines(output_line, lines);
    output = ddr_line(output_line);
    for (word = 0U; word < lines * HPU_WORDS_PER_LINE; ++word) {
        uint32_t expected = mod_add(
            accumulator[word],
            mod_mul(left[word], right[word], MOD_Q0), MOD_Q0);
        if (output[word] != expected) return STEP_ERR_COMPARE;
    }
    return STEP_OK;
}
