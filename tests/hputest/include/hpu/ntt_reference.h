#ifndef HPU_NTT_REFERENCE_H
#define HPU_NTT_REFERENCE_H

#include <stdint.h>

enum {
    HPU_NTT_REFERENCE_N = 4096,
    HPU_NTT_REFERENCE_STAGES = 12
};

struct hpu_ntt_reference {
    uint16_t bit_reverse[HPU_NTT_REFERENCE_N];
    uint32_t input[HPU_NTT_REFERENCE_N];
    uint32_t twist[HPU_NTT_REFERENCE_N];
    uint32_t output[HPU_NTT_REFERENCE_N];
    uint32_t stage_step[HPU_NTT_REFERENCE_STAGES];
    uint32_t modulus;
    uint32_t post_scale;
    uint32_t twist_step;
};

/* 输入和twist均为HPU物理布局；输出为当前P-network NTT物理布局。 */
int hpu_ntt_reference_prepare(struct hpu_ntt_reference *reference,
                              const uint32_t *physical_input,
                              const uint32_t *physical_twist,
                              uint32_t modulus);
void hpu_ntt_reference_run(struct hpu_ntt_reference *reference);

/* 输入为P-network NTT物理布局，post factor为系数域bit-reversed物理布局。 */
int hpu_intt_reference_prepare(struct hpu_ntt_reference *reference,
                               const uint32_t *physical_input,
                               const uint32_t *physical_post_factor,
                               uint32_t modulus);
void hpu_intt_reference_run(struct hpu_ntt_reference *reference);

#endif
