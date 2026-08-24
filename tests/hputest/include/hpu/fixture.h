#ifndef HPU_FIXTURE_H
#define HPU_FIXTURE_H

#include <hpu/cache.h>
#include <hpu/layout.h>
#include <stdint.h>

extern const uint32_t hpu_rns_input_a[HPU_RNS_COEFFICIENTS];
extern const uint32_t hpu_rns_input_b[HPU_RNS_COEFFICIENTS];

static inline uint32_t hpu_fixture_expected_a(unsigned coefficient) {
    return coefficient * 7U + 3U;
}

static inline uint32_t hpu_fixture_expected_b(unsigned coefficient) {
    return coefficient * 11U + 5U;
}

static inline int hpu_fixture_validate_embedded(void) {
    unsigned i;

    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        if (hpu_rns_input_a[i] != hpu_fixture_expected_a(i) ||
            hpu_rns_input_b[i] != hpu_fixture_expected_b(i)) {
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

static inline int hpu_fixture_check_padd(void) {
    volatile uint32_t *output = hpu_line(HPU_LINE_OUTPUT);
    unsigned i;

    hpu_cache_invalidate((uintptr_t)output, HPU_RNS_BYTES);
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        uint32_t expected = (uint32_t)
            (((uint64_t)hpu_rns_input_a[i] + hpu_rns_input_b[i]) %
             HPU_MODULUS);
        if (output[i] != expected) {
            return 1;
        }
    }
    return 0;
}

#endif
