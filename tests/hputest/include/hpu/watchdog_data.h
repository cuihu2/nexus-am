#ifndef HPU_WATCHDOG_DATA_H
#define HPU_WATCHDOG_DATA_H

#include <hpu/fixture.h>
#include <stddef.h>

/* 数据/计数器粒度的小接口：不配置CSR、不发HPU命令、不启动中断。 */
static inline uint64_t wd_cycle(void) {
    uint64_t value;
    __asm__ volatile("rdcycle %0" : "=r"(value) : : "memory");
    return value;
}

static inline int wd_prepare_data(uintptr_t address, uint32_t *expected, unsigned words) {
    if (expected == NULL || words != 4U * WORDS_PER_LINE) return 1;
    volatile uint32_t *memory = (volatile uint32_t *)address;
    /* 两个输入和模表均来自inline-asm；最后一line仅作guard，CPU另存独立影子。 */
    for (unsigned word = 0U; word < words; ++word) {
        expected[word] = word < WORDS_PER_LINE ? RNS_A[word] :
                         word < 2U * WORDS_PER_LINE ? RNS_B[word - WORDS_PER_LINE] :
                         word < 3U * WORDS_PER_LINE ? RNS_MOD_CTX[word - 2U * WORDS_PER_LINE] :
                         UINT32_C(0xa5c30000) ^ word;
        memory[word] = expected[word];
    }
    mem_fence();
    hpu_cache_clean(address, (size_t)words * sizeof(uint32_t));
    return 0;
}

static inline int wd_check_memory(uintptr_t address, const uint32_t *expected, unsigned words) {
    volatile const uint32_t *memory = (volatile const uint32_t *)address;
    hpu_cache_invalidate(address, (size_t)words * sizeof(uint32_t));
    for (unsigned word = 0U; word < words; ++word) {
        const uint32_t actual = memory[word];
        if (actual != expected[word]) {
            LOG_ERROR("[HPU][WATCHDOG][GUARD-FAIL] word=%u actual=0x%x expected=0x%x\n",
                      word, actual, expected[word]);
            return 1;
        }
    }
    return 0;
}
#endif
