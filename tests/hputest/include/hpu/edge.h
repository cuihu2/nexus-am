#ifndef HPU_EDGE_H
#define HPU_EDGE_H
#include <hpu/it_v2.h>
#include <hpu/edge_reference.h>
#include <hpu/encoding.h>
#include <hpu/completion.h>
#include <hpu/result.h>
#include <hpu/trace.h>

/* 软件等待预算，不是硬件看门狗阈值；不启用timer/PLIC。 */
#ifndef EDGE_WAIT_CYCLES
#define EDGE_WAIT_CYCLES UINT64_C(1000000)
#endif
int edge_wait(uint64_t budget);
int edge_failure(const char *file, unsigned line, const char *phase);

/* 实际执行RV64字宽整数指令；与纯C参考分别实现，不能被折叠成常量。 */
static inline __attribute__((always_inline)) uint32_t edge_cpu_step(uint32_t value) {
    uintptr_t state = value, temporary;
    __asm__ volatile(
        "slliw %0, %1, 13\n\txor %1, %1, %0\n\t"
        "srliw %0, %1, 17\n\txor %1, %1, %0\n\t"
        "slliw %0, %1, 5\n\txor %1, %1, %0"
        : "=&r"(temporary), "+r"(state) : : "memory");
    return (uint32_t)state;
}
#endif
