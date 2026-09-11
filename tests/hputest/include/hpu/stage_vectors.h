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

/* 单 stage 物理 loader/P 网络软件 golden；不发指令，不执行完整变换的前后因子。 */
int stage_golden(const uint32_t *input, const uint32_t *twiddle,
                 uint32_t *output, unsigned words, uint32_t q,
                 unsigned stage, unsigned inverse);

#endif
