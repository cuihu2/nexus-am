#ifndef HPU_IT_GENERATED_OPS_H
#define HPU_IT_GENERATED_OPS_H

#include "hpu_test.h"
#include "../third_party/inline-asm/outputs/auto/auto.h"

#include <stdint.h>

int hpu_generated_operator_run(uint32_t seed, hpu_case_kind kind);
size_t hpu_generated_operator_dma_count(hpu_case_kind kind);
int hpu_generated_operator_resolve(hpu_case_kind kind,
                                   hpu_dma_span_t *spans,
                                   size_t span_capacity);

#endif
