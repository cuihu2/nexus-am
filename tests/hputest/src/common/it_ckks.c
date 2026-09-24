#include <hpu/ckks_case.h>
#include <hpu/log.h>
#include <hpu/report.h>

/* 初始密文、评估密钥和常量来自同一次 producer 运行，不在目标机重新生成。 */
int ckks_prepare(void) {
    volatile uint32_t *memory = ddr_line(0U);
    for (unsigned word = 0; word < CKKS_LINES * WORDS_PER_LINE; ++word)
        memory[word] = ckks_window[word];
    clean_lines(0U, CKKS_LINES);
    return 0;
}

int ckks_check_results(void) {
    int failed = 0;
    for (unsigned i = 0; i < CKKS_OUTPUTS; ++i) {
        const struct ckks_output *output = &ckks_outputs[i];
        invalidate_lines(output->line, output->lines);
        LOG_DEBUG("[HPU][CKKS][OUTPUT] component=%u basis=%u line=%u N=%u\n",
                  i / CKKS_Q, i % CKKS_Q, output->line, CKKS_N);
        // 验证最终密文 NTT/RNS 原始字，必须精确一致；不以 CKKS 浮点误差放宽。
        failed |= result_compare("ckks-final", ddr_line(output->line),
                                 ckks_golden + output->golden_word, CKKS_N, 0U);
    }
    return failed;
}

int ckks_check_memory(void) {
    invalidate_lines(0U, CKKS_LINES);
    for (unsigned line = 0; line < CKKS_LINES; ++line) {
        // 只放行真实 DSTORE 涉及的区域，输入、密钥、twiddle 和尾部 guard 都检查。
        if (ckks_writable[line]) continue;
        volatile const uint32_t *actual = ddr_line(line);
        for (unsigned lane = 0; lane < WORDS_PER_LINE; ++lane) {
            const uint32_t expected = ckks_window[line * WORDS_PER_LINE + lane];
            const uint32_t value = actual[lane];
            if (value != expected) {
                LOG_ERROR("[HPU][CKKS][FAIL] phase=readonly-guard line=%u lane=%u "
                          "actual=0x%x expected=0x%x\n", line, lane, value, expected);
                return 1;
            }
        }
    }
    return 0;
}
