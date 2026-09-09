#ifndef HPU_STAGE_VECTORS_H
#define HPU_STAGE_VECTORS_H

#include <stdint.h>

/* 表来自同批 inline-asm reference；导入时逐项验证，禁止使用旧 outputs 缓存。 */
enum { STAGE_WORDS = 2048, STAGE_LINES = 32, STAGE_MODULUS = 50061313 };

extern const uint32_t ntt_twiddle_0[STAGE_WORDS];
extern const uint32_t ntt_twiddle_1[STAGE_WORDS];
extern const uint32_t ntt_twiddle_11[STAGE_WORDS];
extern const uint32_t intt_twiddle_0[STAGE_WORDS];
extern const uint32_t intt_twiddle_1[STAGE_WORDS];
extern const uint32_t intt_twiddle_11[STAGE_WORDS];

#endif
