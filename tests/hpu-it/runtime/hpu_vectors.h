#ifndef HPU_IT_VECTORS_H
#define HPU_IT_VECTORS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HPU_VECTOR_NTT = 0,
    HPU_VECTOR_INTT = 1
} hpu_vector_kind;

typedef struct {
    const uint8_t *image_begin;
    const uint8_t *image_end;
    uint64_t fnv1a64;
    unsigned image_lines;
    unsigned input_line;
    unsigned expected_line;
    unsigned mod_ctx_line;
    unsigned pre_twist_line;
    unsigned ntt_stage_line;
    unsigned intt_stage_line;
    unsigned post_scale_line;
    unsigned data_lines;
    unsigned limb_lines;
    unsigned stage_lines;
    unsigned stages;
    unsigned basis_count;
    unsigned basis_stride_lines;
} hpu_vector_image;

const hpu_vector_image *hpu_get_vector_image(hpu_vector_kind kind);
size_t hpu_vector_image_size(const hpu_vector_image *image);

#endif
