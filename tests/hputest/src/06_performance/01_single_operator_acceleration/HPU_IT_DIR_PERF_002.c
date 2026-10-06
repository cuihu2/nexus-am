#include <hpu/completion.h>
#include <hpu/log.h>
#include <hpu/ntt_reference.h>
#include <hpu/perf.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <hpu/transform.h>
#include <ntt/delivery.h>

#ifndef HPU_PERF_WARMUP_ROUNDS
#define HPU_PERF_WARMUP_ROUNDS 1U
#endif
#ifndef HPU_PERF_MEASURE_ROUNDS
#define HPU_PERF_MEASURE_ROUNDS 3U
#endif
#if HPU_PERF_MEASURE_ROUNDS < 2 || HPU_PERF_MEASURE_ROUNDS > HPU_PERF_MAX_SAMPLES
#error "HPU_PERF_MEASURE_ROUNDS must be in [2, HPU_PERF_MAX_SAMPLES]"
#endif

#define CASE_ID "HPU_IT_DIR_PERF_002"

_Static_assert((unsigned)TRANSFORM_N == (unsigned)HPU_NTT_REFERENCE_N,
               "PERF002 reference and transform delivery size differ");
static struct hpu_ntt_reference cpu_reference;

static int configure_hpu(void) {
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, TRANSFORM_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, TRANSFORM_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return 1;
    csr_write(CSR_COMMIT, COMMIT);
    return wait_window(1) != 0 || check_status() != 0;
}

static int run_hpu_round(uint64_t *cycles) {
    if (cycles == NULL || transform_prepare(transform_ntt_image) != 0) return 1;

    const uint64_t begin = perf_cycle_read();
    int rc = hpu_program_ntt(transform_ntt_spans, HPU_PROGRAM_NTT_DMA_COUNT);
    if (rc == 0) rc = wait_irq();
    const uint64_t end = perf_cycle_read();
    *cycles = end - begin;

    if (rc != 0 || completion_clear() != 0 || check_status() != 0)
        return 1;
    if (transform_check_result(transform_ntt_golden) != 0 ||
        transform_check_memory(transform_ntt_image) != 0)
        return 1;
    return 0;
}

/*
 * 测试点：IT-PERF-002
 * 目的：在同一输入、参数和golden下比较CPU参考NTT与HPU完整提交路径的cycle。
 * CPU 与 HPU 使用 CMB002 的同一 N4096/Q0 输入和 exact golden。
 * cpu-compute 只包含可移植参考 NTT；hpu-submit-to-done 从完整 producer
 * 程序调用前计到完成 IRQ 且 BUSY 清零，包含发令、DMA、反压和等待。
 * 这两个 CPU cycle 指标都不是 HPU 内核纯计算 cycle；后者须由 RTL monitor 提供。
 * 每轮在计时区间外检查完整结果和只读 guard。speedup 仅报告，不设通过门限。
 */
int main(void) {
    struct perf_samples cpu_samples;
    struct perf_samples hpu_samples;
    const unsigned total_rounds = HPU_PERF_WARMUP_ROUNDS + HPU_PERF_MEASURE_ROUNDS;

    case_start(__FILE__);
    if (progress_begin(__FILE__, 1U) != 0) return case_fail(__FILE__, __LINE__);
    perf_cycle_enable();
    perf_samples_init(&cpu_samples);
    perf_samples_init(&hpu_samples);

    phase_mark("prepare-reference");
    if (hpu_ntt_reference_prepare(
            &cpu_reference,
            transform_ntt_image + TRANSFORM_INPUT * WORDS_PER_LINE,
            transform_ntt_image + TRANSFORM_FACTOR * WORDS_PER_LINE,
            TRANSFORM_Q) != 0)
        return case_fail(__FILE__, __LINE__);
    transform_print_bindings(transform_ntt_bindings, HPU_PROGRAM_NTT_DMA_COUNT);
    phase_mark("configure");
    if (configure_hpu() != 0) return case_fail(__FILE__, __LINE__);

    for (unsigned round = 0U; round < total_rounds; ++round) {
        uint64_t hpu_cycles;
        if (result_context(__FILE__, round) != 0) return case_fail(__FILE__, __LINE__);

        const uint64_t cpu_begin = perf_cycle_read();
        hpu_ntt_reference_run(&cpu_reference);
        const uint64_t cpu_cycles = perf_cycle_read() - cpu_begin;
        if (result_compare("perf-cpu-ntt", cpu_reference.output, transform_ntt_golden,
                           TRANSFORM_N, TRANSFORM_Q) != 0)
            return case_fail(__FILE__, __LINE__);

        if (run_hpu_round(&hpu_cycles) != 0)
            return case_fail(__FILE__, __LINE__);

        if (round >= HPU_PERF_WARMUP_ROUNDS) {
            if (perf_samples_record(&cpu_samples, cpu_cycles) != 0 ||
                perf_samples_record(&hpu_samples, hpu_cycles) != 0)
                return case_fail(__FILE__, __LINE__);
        }
    }

    phase_mark("report");
    perf_samples_report(CASE_ID, "cpu-compute", &cpu_samples);
    perf_samples_report(CASE_ID, "hpu-submit-to-done", &hpu_samples);
    LOG_EVENT("[HPU][PERF][SPEEDUP] case=%s numerator=cpu-compute "
              "denominator=hpu-submit-to-done x1000=%lu threshold=none\n",
              CASE_ID, (unsigned long)perf_speedup_x1000(
                  perf_samples_average(&cpu_samples),
                  perf_samples_average(&hpu_samples)));
    LOG_EVENT("[HPU][PERF][SCOPE] hpu-pure-compute=NOT_MEASURED "
              "required-source=RTL-monitor\n");
    phase_mark("case-done");
    return case_pass(__FILE__);
}
