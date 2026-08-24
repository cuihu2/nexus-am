#ifndef HPU_FIXTURE_H
#define HPU_FIXTURE_H

#include <hpu/cache.h>
#include <hpu/layout.h>
#include <klib.h>
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
            printf("HPU_SMOKE_FIXTURE_FAIL coefficient=%u a=0x%x b=0x%x\n",
                   i, hpu_rns_input_a[i], hpu_rns_input_b[i]);
            return 1;
        }
    }
    printf("HPU_SMOKE_FIXTURE_PASS rns_components=2 coefficients=%u "
           "bytes_per_component=%u\n",
           HPU_RNS_COEFFICIENTS, (unsigned)HPU_RNS_BYTES);
    return 0;
}

static inline void hpu_fixture_copy_inputs_to_ddr(void) {
    volatile uint32_t *input_a = hpu_line(HPU_LINE_SRC_A);
    volatile uint32_t *input_b = hpu_line(HPU_LINE_SRC_B);
    unsigned i;

    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        input_a[i] = hpu_rns_input_a[i];
        input_b[i] = hpu_rns_input_b[i];
    }
    hpu_fence();
    hpu_cache_clean((uintptr_t)input_a, HPU_RNS_BYTES);
    hpu_cache_clean((uintptr_t)input_b, HPU_RNS_BYTES);
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
            printf("HPU_SMOKE_MISMATCH coefficient=%u actual=0x%x "
                   "expected=0x%x\n",
                   i, output[i], hpu_rns_input_a[i]);
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
            printf("HPU_SMOKE_MISMATCH coefficient=%u actual=0x%x "
                   "expected=0x%x\n",
                   i, output[i], expected);
            return 1;
        }
    }
    return 0;
}

#endif
