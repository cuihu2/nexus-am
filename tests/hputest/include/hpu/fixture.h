#ifndef HPU_FIXTURE_H
#define HPU_FIXTURE_H

#include <hpu/cache.h>
#include <hpu/layout.h>
#include <klib.h>
#include <stdint.h>

extern const uint32_t RNS_A[HPU_RNS_COEFFICIENTS];
extern const uint32_t RNS_B[HPU_RNS_COEFFICIENTS];
extern const uint32_t RNS_EXPECTED[HPU_RNS_COEFFICIENTS];
extern const uint32_t RNS_MOD_CTX[HPU_WORDS_PER_LINE];

static inline int fixture_validate(void) {
    unsigned i;

    /* 分别检查生成库提供的 A/B；仅打印首个越界系数。 */
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        if (RNS_A[i] >= HPU_MODULUS) {
            printf("[HPU][FAIL][fixture] input=A index=%u actual=0x%x "
                   "expected=<q q=%u\n", i, RNS_A[i], HPU_MODULUS);
            return 1;
        }
        if (RNS_B[i] >= HPU_MODULUS) {
            printf("[HPU][FAIL][fixture] input=B index=%u actual=0x%x "
                   "expected=<q q=%u\n", i, RNS_B[i], HPU_MODULUS);
            return 1;
        }
    }
    return 0;
}

static inline int fixture_validate_mm(void) {
    const uint64_t expected_mu = UINT64_MAX / HPU_MODULUS;
    const uint32_t expected_words[4] = {
        HPU_MODULUS, (uint32_t)expected_mu,
        (uint32_t)(expected_mu >> 32U), 0U
    };
    unsigned i;

    /* 逐字检查 MM basis 0 的 q32 + Barrett-mu48，保留原先的首错返回。 */
    for (i = 0U; i < 4U; ++i) {
        if (RNS_MOD_CTX[i] != expected_words[i]) {
            printf("[HPU][FAIL][mod-context] word=%u actual=0x%x "
                   "expected=0x%x q=%u\n",
                   i, RNS_MOD_CTX[i], expected_words[i], HPU_MODULUS);
            return 1;
        }
    }
    for (i = 4U; i < HPU_WORDS_PER_LINE; ++i) {
        if (RNS_MOD_CTX[i] != 0U) {
            printf("[HPU][FAIL][mod-context-padding] index=%u actual=0x%x "
                   "expected=0x0 q=%u\n", i, RNS_MOD_CTX[i], HPU_MODULUS);
            return 1;
        }
    }

    /* 发指令前先用 C 计算独立校验生成库 golden，不读取 HPU 输出。 */
    for (i = 0U; i < HPU_RNS_COEFFICIENTS; ++i) {
        const uint32_t c_oracle = (uint32_t)
            (((uint64_t)RNS_A[i] * RNS_B[i]) % HPU_MODULUS);

        if (RNS_EXPECTED[i] != c_oracle) {
            printf("[HPU][FAIL][fixture-golden-vs-C] index=%u A=0x%x B=0x%x "
                   "golden=0x%x expected=0x%x q=%u\n",
                   i, RNS_A[i], RNS_B[i], RNS_EXPECTED[i], c_oracle,
                   HPU_MODULUS);
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
        /* volatile 输出只读一次，错误日志使用同一次读取结果。 */
        const uint32_t actual = output[i];

        if (actual != RNS_A[i]) {
            printf("[HPU][FAIL][loopback] input=A index=%u actual=0x%x "
                   "expected=0x%x q=%u\n",
                   i, actual, RNS_A[i], HPU_MODULUS);
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

        /* 保留原来的短路：golden 错误时不读取 HPU 输出。 */
        if (RNS_EXPECTED[i] != c_oracle) {
            printf("[HPU][FAIL][pmul-golden-vs-C] index=%u A=0x%x B=0x%x "
                   "golden=0x%x expected=0x%x q=%u\n",
                   i, RNS_A[i], RNS_B[i], RNS_EXPECTED[i], c_oracle,
                   HPU_MODULUS);
            return 1;
        }

        /* 仅首个 HPU 结果错误打印明细，不在逐系数成功路径打印。 */
        const uint32_t actual = output[i];

        if (actual != c_oracle) {
            printf("[HPU][FAIL][pmul-HPU-vs-C] index=%u A=0x%x B=0x%x "
                   "actual=0x%x expected=0x%x q=%u\n",
                   i, RNS_A[i], RNS_B[i], actual, c_oracle, HPU_MODULUS);
            return 1;
        }
    }
    return 0;
}

#endif
