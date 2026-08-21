#ifndef HPU_IT_FHE_RUNTIME_H
#define HPU_IT_FHE_RUNTIME_H

#include "hpu_test.h"
#include "../third_party/inline-asm/outputs/keyswitch/keyswitch.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Run one of the delivery-sized FHE programs.  Functional callers may pass
 * NULL cycle pointers.  Performance callers receive the explicit CPU
 * reference and HPU issue-to-terminal-PSYNC intervals.
 */
int hpu_fhe_run(uint32_t seed, hpu_case_kind kind,
                uint64_t *cpu_compute_cycles,
                uint64_t *hpu_compute_cycles,
                uint64_t *cpu_e2e_cycles,
                uint64_t *hpu_e2e_cycles);

/* Resolve every generated composite-program DMA in issue order. */
int hpu_fhe_resolve_generated_program(hpu_case_kind kind,
                                      hpu_dma_span_t *spans,
                                      size_t span_capacity,
                                      size_t *span_count);

#endif
