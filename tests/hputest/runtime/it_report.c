#include <hpu/report.h>
#include <klib.h>

#include <limits.h>
#include <stddef.h>

#ifndef HPU_DUMP_RESULTS
#define HPU_DUMP_RESULTS 0
#endif

#if HPU_DUMP_RESULTS != 0 && HPU_DUMP_RESULTS != 1
#error "HPU_DUMP_RESULTS must be 0 or 1"
#endif
#if ULONG_MAX <= UINT32_MAX
#error "HPU UART reporting requires a 64-bit long (RV64 or 64-bit host)"
#endif

static const char *current_case;
static unsigned current_round;
static unsigned next_block;

static int valid_name(const char *text) {
    if (text == NULL || *text == '\0') return 0;
    for (; *text != '\0'; ++text) {
        const unsigned char ch = (unsigned char)*text;
        if (ch <= 0x20U || ch >= 0x7fU || ch == ',') return 0;
    }
    return 1;
}

int result_context(const char *case_id, unsigned round) {
    current_case = NULL;
    if (!valid_name(case_id)) {
        printf("[HPU][RESULT] ERROR,invalid-case-context\n");
        return 1;
    }
    current_case = case_id;
    current_round = round;
    next_block = 0U;
    return 0;
}

int result_compare(const char *phase, volatile const uint32_t *actual,
                   const uint32_t *golden, unsigned words, uint32_t q) {
    unsigned mismatch = 0U;
    unsigned emitted = 0U;
    unsigned printed_bad = 0U;
    unsigned q_multiple = 0U;
    unsigned noncanonical = 0U;
    uint32_t max_abs_diff = 0U;
    long first_bad = -1L;
    unsigned index;

    if (current_case == NULL || !valid_name(phase) || actual == NULL ||
        golden == NULL || words == 0U || next_block == UINT_MAX) {
        printf("[HPU][RESULT] ERROR,invalid-comparison-arguments\n");
        return 1;
    }

    /* 协议 v1：BEGIN,版本,模式,case,phase,round,block,words,q,DDR 地址。 */
    printf("[HPU][RESULT] BEGIN,1,%s,%s,%s,%u,%u,%u,%u,0x%lx\n",
           HPU_DUMP_RESULTS ? "full" : "brief", current_case, phase,
           current_round, next_block++, words, (unsigned)q,
           (unsigned long)(uintptr_t)actual);

    for (index = 0U; index < words; ++index) {
        /* 保存本次采样后才打印；不能为诊断反复读取可能仍在变化的 DDR。 */
        const uint32_t value = actual[index];
        const uint32_t expected = golden[index];
        const long delta = (long)value - (long)expected;
        const uint32_t absolute = (uint32_t)(delta < 0L ? -delta : delta);
        const int outside = q != 0U && value >= q;
        const int bad = value != expected || outside;

        if (absolute > max_abs_diff) max_abs_diff = absolute;
        if (outside) ++noncanonical;
        if (bad) {
            if (first_bad < 0L) first_bad = (long)index;
            ++mismatch;
            /* 只对真实差值做昂贵的除法；delta=0 的非规范数不算相差 q。 */
            if (q != 0U && delta != 0L && delta % (long)q == 0L)
                ++q_multiple;
        }
        if (HPU_DUMP_RESULTS || index < 4U || (bad && printed_bad < 8U)) {
            printf("[HPU][RESULT] DATA,%u,0x%x,0x%x,%u,%ld\n", index,
                   (unsigned)value, (unsigned)expected, (unsigned)q, delta);
            ++emitted;
            if (bad) ++printed_bad;
        }
    }

    /* END 的 words/emitted 可检查截断；FAIL 也输出完整统计而非首错返回。 */
    printf("[HPU][RESULT] END,%u,%u,%u,%ld,%u,%u,%u,%s\n", words,
           emitted, mismatch, first_bad, (unsigned)max_abs_diff, q_multiple,
           noncanonical, mismatch == 0U ? "PASS" : "FAIL");
    printf("[HPU][CHECK] case=%s phase=%s round=%u words=%u mismatches=%u "
           "first_bad=%ld max_abs_diff=%u q_multiple_mismatches=%u "
           "noncanonical=%u exact_integer=1\n", current_case, phase,
           current_round, words, mismatch, first_bad, (unsigned)max_abs_diff,
           q_multiple, noncanonical);
    return mismatch != 0U ? 1 : 0;
}
