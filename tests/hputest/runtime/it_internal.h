#ifndef HPU_TEST_INTERNAL_H
#define HPU_TEST_INTERNAL_H

#include <hpu/cache.h>
#include <hpu/csr.h>
#include <hpu/encoding.h>
#include <hpu/steps.h>

#include <stddef.h>
#include <stdint.h>

enum {
    STEP_OK = 0,
    STEP_ERR_ARGUMENT = 201,
    STEP_ERR_CSR_READBACK = 202,
    STEP_ERR_WINDOW = 203,
    STEP_ERR_FAULT = 204,
    STEP_ERR_TIMEOUT = 205,
    STEP_ERR_COMPARE = 206,
    STEP_ERR_ENCODING_UNAVAILABLE = 207,
    STEP_ERR_NOT_ISSUED = 208
};

_Static_assert(POLY_WORDS == 4096U,
               "migrated IT cases require one 4096-coefficient RNS limb");
_Static_assert(MOD_Q0 == HPU_MODULUS,
               "runtime modulus must match the inline-asm MM fixture");

extern const uint32_t RNS_A[POLY_WORDS];
extern const uint32_t RNS_B[POLY_WORDS];

volatile uint32_t *ddr_line(unsigned line);
void clean_lines(unsigned first, unsigned lines);
void invalidate_lines(unsigned first, unsigned lines);
void fill_line(unsigned line, uint32_t seed, uint32_t modulus);
void poison_line(unsigned line, uint32_t salt);
int check_line(unsigned line, const uint32_t *expected, unsigned words);

uint32_t mod_add(uint32_t a, uint32_t b, uint32_t modulus);
uint32_t mod_sub(uint32_t a, uint32_t b, uint32_t modulus);
uint32_t mod_mul(uint32_t a, uint32_t b, uint32_t modulus);

#endif
