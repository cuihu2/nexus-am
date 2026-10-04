#ifndef HPU_TRACE_H
#define HPU_TRACE_H
#include <stdint.h>

enum { TRACE_PREPARE=1, TRACE_CONFIGURE, TRACE_ISSUE, TRACE_WAIT,
       TRACE_COMPARE, TRACE_GUARD, TRACE_DONE };
struct hpu_trace_record {
    uint32_t magic, phase, instruction, dma, word, status, irq, fault;
    uint64_t wait_cycles;
};
extern volatile struct hpu_trace_record application_trace;

/* 仅写内存诊断记录，不打印、不读 MMIO，不启动计时器或中断。 */
static inline void trace_phase(uint32_t phase) {
    application_trace.magic = UINT32_C(0x48505554);
    application_trace.phase = phase;
}
static inline void trace_issue(uint32_t instruction, uint32_t dma, uint32_t word) {
    application_trace.instruction = instruction;
    application_trace.dma = dma;
    application_trace.word = word;
}
#endif
