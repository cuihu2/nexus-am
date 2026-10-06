#ifndef HPU_PERF_H
#define HPU_PERF_H

#include <stdint.h>

#ifndef HPU_PERF_MAX_SAMPLES
#define HPU_PERF_MAX_SAMPLES 16U
#endif

struct perf_samples {
    uint64_t values[HPU_PERF_MAX_SAMPLES];
    uint64_t total;
    uint64_t minimum;
    uint64_t maximum;
    unsigned count;
};

/* 必须在仍有 M-mode CSR 权限时调用；性能计数不依赖日志级别。 */
void perf_cycle_enable(void);

/* fence 后读取 CPU cycle。它不是 HPU 内核的纯计算计数器。 */
uint64_t perf_cycle_read(void);

void perf_samples_init(struct perf_samples *samples);
int perf_samples_record(struct perf_samples *samples, uint64_t cycles);
uint64_t perf_samples_average(const struct perf_samples *samples);
uint64_t perf_speedup_x1000(uint64_t cpu_cycles, uint64_t hpu_cycles);
void perf_samples_report(const char *case_id, const char *metric,
                         const struct perf_samples *samples);

#endif
