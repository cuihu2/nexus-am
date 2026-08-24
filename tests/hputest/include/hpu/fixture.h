#ifndef HPU_FIXTURE_H
#define HPU_FIXTURE_H

#include <hpu/cache.h>
#include <hpu/layout.h>
#include <stdint.h>

extern const uint32_t hpu_rns_input_a[HPU_RNS_COEFFICIENTS];
extern const uint32_t hpu_rns_input_b[HPU_RNS_COEFFICIENTS];
extern const uint32_t hpu_rns_expected[HPU_RNS_COEFFICIENTS];
extern const uint32_t hpu_rns_mod_ctx[HPU_WORDS_PER_LINE];

static inline int hpu_fixture_validate_embedded(void) {
    unsigned i;

    /* Every smoke case carries exactly the two producer-owned input limbs. */
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        if (hpu_rns_input_a[i] >= HPU_MODULUS ||
            hpu_rns_input_b[i] >= HPU_MODULUS) {
            return 1;
        }
    }
    return 0;
}

static inline int hpu_fixture_validate_mm_assets(void) {
    const uint64_t expected_mu = UINT64_MAX / HPU_MODULUS;
    unsigned i;

    /* Exact q32 + Barrett-mu48 record emitted by inline-asm for MM basis 0. */
    if (hpu_rns_mod_ctx[0] != HPU_MODULUS ||
        hpu_rns_mod_ctx[1] != (uint32_t)expected_mu ||
        hpu_rns_mod_ctx[2] != (uint32_t)(expected_mu >> 32U) ||
        hpu_rns_mod_ctx[3] != 0U) {
        return 1;
    }
    for (i = 4U; i < HPU_WORDS_PER_LINE; ++i) {
        if (hpu_rns_mod_ctx[i] != 0U) return 1;
    }

    /* Validate the producer golden independently before issuing HPU work. */
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        const uint32_t c_oracle = (uint32_t)
            (((uint64_t)hpu_rns_input_a[i] * hpu_rns_input_b[i]) %
             HPU_MODULUS);

        if (hpu_rns_expected[i] != c_oracle) {
            return 1;
        }
    }
    return 0;
}

static inline void hpu_fixture_copy_to_ddr(unsigned destination_line,
                                           const uint32_t *source) {
    volatile uint32_t *destination = hpu_line(destination_line);
    unsigned i;

    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        destination[i] = source[i];
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)destination, HPU_RNS_BYTES);
}

static inline void hpu_fixture_copy_mod_ctx_to_ddr(void) {
    volatile uint32_t *destination = hpu_line(HPU_LINE_MOD);
    unsigned i;

    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        destination[i] = hpu_rns_mod_ctx[i];
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)destination, HPU_LINE_BYTES);
}

static inline void hpu_fixture_poison_output(void) {
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    unsigned i;

    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        output[i] = UINT32_C(0xa5a50000) ^ i;
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)output, HPU_RNS_BYTES);
}

static inline int hpu_fixture_check_dload_dstore(void) {
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    unsigned i;

    hpu_cache_invalidate((uintptr_t)output, HPU_RNS_BYTES);
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        if (output[i] != hpu_rns_input_a[i]) {
            return 1;
        }
    }
    return 0;
}

static inline int hpu_fixture_check_pmul(void) {
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    unsigned i;

    hpu_cache_invalidate((uintptr_t)output, HPU_RNS_BYTES);
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        const uint32_t c_oracle = (uint32_t)
            (((uint64_t)hpu_rns_input_a[i] * hpu_rns_input_b[i]) %
             HPU_MODULUS);

        if (hpu_rns_expected[i] != c_oracle || output[i] != c_oracle) {
            return 1;
        }
    }
    return 0;
}

#endif
