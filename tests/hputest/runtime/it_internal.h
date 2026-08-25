#ifndef HPU_IT_INTERNAL_H
#define HPU_IT_INTERNAL_H

#include <hpu/cache.h>
#include <hpu/csr.h>
#include <hpu/encoding.h>
#include <hpu/it_case_steps.h>

#include <stddef.h>
#include <stdint.h>

enum {
    HPU_IT_OK = 0,
    HPU_IT_ERR_ARGUMENT = 201,
    HPU_IT_ERR_CSR_READBACK = 202,
    HPU_IT_ERR_WINDOW = 203,
    HPU_IT_ERR_FAULT = 204,
    HPU_IT_ERR_TIMEOUT = 205,
    HPU_IT_ERR_COMPARE = 206,
    HPU_IT_ERR_ENCODING_UNAVAILABLE = 207,
    HPU_IT_ERR_NOT_ISSUED = 208
};

_Static_assert(HPU_IT_COEFFICIENTS == 4096U,
               "migrated IT cases require one 4096-coefficient RNS limb");
_Static_assert(HPU_IT_Q0 == HPU_MODULUS,
               "runtime modulus must match the inline-asm MM fixture");

extern const uint32_t hpu_rns_input_a[HPU_IT_COEFFICIENTS];
extern const uint32_t hpu_rns_input_b[HPU_IT_COEFFICIENTS];

volatile uint32_t *hpu_it_line(unsigned line);
void hpu_it_clean_lines(unsigned first, unsigned lines);
void hpu_it_invalidate_lines(unsigned first, unsigned lines);
void hpu_it_fill_line(unsigned line, uint32_t seed, uint32_t modulus);
void hpu_it_poison_line(unsigned line, uint32_t salt);
int hpu_it_compare_line(unsigned line, const uint32_t *expected,
                        unsigned words);

uint32_t hpu_it_mod_add(uint32_t a, uint32_t b, uint32_t modulus);
uint32_t hpu_it_mod_sub(uint32_t a, uint32_t b, uint32_t modulus);
uint32_t hpu_it_mod_mul(uint32_t a, uint32_t b, uint32_t modulus);

#endif
