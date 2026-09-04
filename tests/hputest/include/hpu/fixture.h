#ifndef HPU_FIXTURE_H
#define HPU_FIXTURE_H

#include <hpu/cache.h>
#include <hpu/layout.h>
#include <stdint.h>

extern const uint32_t RNS_A[HPU_RNS_COEFFICIENTS];
extern const uint32_t RNS_B[HPU_RNS_COEFFICIENTS];
extern const uint32_t RNS_EXPECTED[HPU_RNS_COEFFICIENTS];
extern const uint32_t RNS_MOD_CTX[HPU_WORDS_PER_LINE];

static inline int fixture_validate(void) {
    unsigned i;

    /* Every smoke case carries exactly the two producer-owned input limbs. */
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        if (RNS_A[i] >= HPU_MODULUS || RNS_B[i] >= HPU_MODULUS) {
            return 1;
        }
    }
    return 0;
}

static inline int fixture_validate_mm(void) {
    const uint64_t expected_mu = UINT64_MAX / HPU_MODULUS;
    unsigned i;

    /* Exact q32 + Barrett-mu48 record emitted by inline-asm for MM basis 0. */
    if (RNS_MOD_CTX[0] != HPU_MODULUS ||
        RNS_MOD_CTX[1] != (uint32_t)expected_mu ||
        RNS_MOD_CTX[2] != (uint32_t)(expected_mu >> 32U) ||
        RNS_MOD_CTX[3] != 0U) {
        return 1;
    }
    for (i = 4U; i < HPU_WORDS_PER_LINE; ++i) {
        if (RNS_MOD_CTX[i] != 0U) return 1;
    }

    /* Validate the producer golden independently before issuing HPU work. */
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        const uint32_t c_oracle = (uint32_t)
            (((uint64_t)RNS_A[i] * RNS_B[i]) % HPU_MODULUS);

        if (RNS_EXPECTED[i] != c_oracle) {
            return 1;
        }
    }
    return 0;
}

static inline void fixture_copy(unsigned destination_line,
                                const uint32_t *source) {
    volatile uint32_t *destination = hpu_line(destination_line);
    unsigned i;

    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        destination[i] = source[i];
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)destination, HPU_RNS_BYTES);
}

static inline void fixture_copy_mod(void) {
    volatile uint32_t *destination = hpu_line(HPU_LINE_MOD);
    unsigned i;

    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        destination[i] = RNS_MOD_CTX[i];
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)destination, HPU_LINE_BYTES);
}

static inline void fixture_poison(void) {
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    unsigned i;

    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        output[i] = UINT32_C(0xa5a50000) ^ i;
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)output, HPU_RNS_BYTES);
}

static inline int check_loopback(void) {
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    unsigned i;

    hpu_cache_invalidate((uintptr_t)output, HPU_RNS_BYTES);
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        if (output[i] != RNS_A[i]) {
            return 1;
        }
    }
    return 0;
}

static inline int check_pmul(void) {
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    unsigned i;

    hpu_cache_invalidate((uintptr_t)output, HPU_RNS_BYTES);
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        const uint32_t c_oracle = (uint32_t)
            (((uint64_t)RNS_A[i] * RNS_B[i]) % HPU_MODULUS);

        if (RNS_EXPECTED[i] != c_oracle || output[i] != c_oracle) {
            return 1;
        }
    }
    return 0;
}

#endif
