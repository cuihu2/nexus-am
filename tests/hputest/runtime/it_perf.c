#include <hpu/log.h>
#include <hpu/perf.h>

#include <limits.h>
#include <stddef.h>

#ifdef HPU_PERF_HOST_TEST
extern void perf_host_enable_cycle(void);
extern uint64_t perf_host_cycle(void);
#endif

void perf_cycle_enable(void) {
#ifdef HPU_PERF_HOST_TEST
    perf_host_enable_cycle();
#else
    /* main 从 M 模式开始；只开放 cycle，不改其它 mcounteren 位。 */
    __asm__ volatile("csrs mcounteren, %0" : : "r"((uintptr_t)1U) : "memory");
#endif
}

uint64_t perf_cycle_read(void) {
#ifdef HPU_PERF_HOST_TEST
    return perf_host_cycle();
#else
    uint64_t cycle;
    /* 端点前完成普通内存访问，并阻止编译器把被测代码移过计数点。 */
    __asm__ volatile("fence rw, rw\n\trdcycle %0" : "=r"(cycle) : : "memory");
    return cycle;
#endif
}

void perf_samples_init(struct perf_samples *samples) {
    if (samples == NULL) return;
    samples->total = 0U;
    samples->minimum = UINT64_MAX;
    samples->maximum = 0U;
    samples->count = 0U;
}

int perf_samples_record(struct perf_samples *samples, uint64_t cycles) {
    if (samples == NULL || cycles == 0U ||
        samples->count >= HPU_PERF_MAX_SAMPLES ||
        UINT64_MAX - samples->total < cycles)
        return 1;
    samples->values[samples->count++] = cycles;
    samples->total += cycles;
    if (cycles < samples->minimum) samples->minimum = cycles;
    if (cycles > samples->maximum) samples->maximum = cycles;
    return 0;
}

uint64_t perf_samples_average(const struct perf_samples *samples) {
    if (samples == NULL || samples->count == 0U) return 0U;
    return samples->total / samples->count;
}

uint64_t perf_speedup_x1000(uint64_t cpu_cycles, uint64_t hpu_cycles) {
    if (hpu_cycles == 0U) return 0U;
    const uint64_t whole = cpu_cycles / hpu_cycles;
    if (whole > UINT64_MAX / 1000U) return UINT64_MAX;
    uint64_t remainder = cpu_cycles % hpu_cycles;
    uint64_t fractional = 0U;
    /* 逐位求三位小数，避免 remainder * 1000 溢出或引入 __udivti3。 */
    for (unsigned decimal = 0U; decimal < 3U; ++decimal) {
        unsigned digit = 0U;
        uint64_t next = 0U;
        for (unsigned add = 0U; add < 10U; ++add) {
            if (next >= hpu_cycles - remainder) {
                next -= hpu_cycles - remainder;
                ++digit;
            } else {
                next += remainder;
            }
        }
        remainder = next;
        fractional = fractional * 10U + digit;
    }
    return whole * 1000U + fractional;
}

void perf_samples_report(const char *case_id, const char *metric,
                         const struct perf_samples *samples) {
    if (case_id == NULL || metric == NULL || samples == NULL || samples->count == 0U) {
        LOG_ERROR("[HPU][PERF][FAIL] reason=invalid-report\n");
        return;
    }
    for (unsigned round = 0U; round < samples->count; ++round)
        LOG_EVENT("[HPU][PERF][SAMPLE] case=%s metric=%s round=%u cycles=%lu\n",
                  case_id, metric, round, (unsigned long)samples->values[round]);
    LOG_EVENT("[HPU][PERF][STATS] case=%s metric=%s rounds=%u average=%lu "
              "minimum=%lu maximum=%lu spread=%lu\n",
              case_id, metric, samples->count,
              (unsigned long)perf_samples_average(samples),
              (unsigned long)samples->minimum, (unsigned long)samples->maximum,
              (unsigned long)(samples->maximum - samples->minimum));
}
