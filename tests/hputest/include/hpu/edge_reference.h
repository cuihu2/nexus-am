#ifndef HPU_EDGE_REFERENCE_H
#define HPU_EDGE_REFERENCE_H
#include <stdint.h>

/* 独立CPU参考：只计算数据，不读MMIO、不发HPU命令。 */
static inline uint32_t edge_reference_step(uint32_t value) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

/* even为PADD，odd为PMUL立即数7；顺序不同通常产生不同结果。 */
static inline uint32_t edge_reference_burst(uint32_t a, uint32_t b,
                                          unsigned commands, uint32_t q) {
    uint64_t value = a;
    for (unsigned command = 0; command < commands; ++command)
        value = (command & 1U) ? value * 7U % q : (value + b) % q;
    return (uint32_t)value;
}

/* memory/branch用例的CPU参考；scratch与实际CPU数组分离。 */
static inline uint32_t edge_reference_memory(uint32_t state, unsigned command,
                                            uint32_t scratch[8], uint32_t *choice) {
    state = edge_reference_step(state ^ command);
    unsigned index = (state >> 3) & 7U;
    uint32_t loaded = state ^ (UINT32_C(0x9e3779b9) * (command + 1U));
    scratch[index] = loaded;
    *choice = loaded & 1U;  /* 1为PADD，0为PMUL立即数7。 */
    return loaded ^ scratch[(index + 1U) & 7U];
}
#endif
