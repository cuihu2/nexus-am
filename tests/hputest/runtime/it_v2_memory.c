#include <hpu/it_v2.h>
#include <hpu/report.h>
#include <klib.h>

enum { WINDOW_WORDS = WINDOW_LINES * WORDS_PER_LINE };

extern const uint32_t RNS_A[POLY_WORDS];
extern const uint32_t RNS_B[POLY_WORDS];

/* 位于 CPU 的 BSS，不放进供 HPU DMA 访问的 0x87000000 窗口。 */
static uint32_t shadow[WINDOW_WORDS];
static unsigned char output_allowed[WINDOW_LINES];
static int prepared;

static int word_range(const char *operation, unsigned line, unsigned words) {
    if (!prepared || line >= WINDOW_LINES || words == 0U ||
        words > (WINDOW_LINES - line) * WORDS_PER_LINE) {
        printf("[HPU][FAIL][%s] line=%u words=%u window_lines=%u prepared=%d\n",
               operation, line, words, WINDOW_LINES, prepared);
        return 1;
    }
    return 0;
}

static unsigned line_count(unsigned words) {
    return (words + WORDS_PER_LINE - 1U) / WORDS_PER_LINE;
}

static uint32_t guard_value(unsigned word) {
    /* guard 随地址变化，便于定位串写；它不是模运算输入。 */
    return UINT32_C(0xd15ea5ed) ^ (word * UINT32_C(0x9e3779b1));
}

static int source_range(const char *operation,
                         const uint32_t *source, unsigned words) {
    const uintptr_t start = (uintptr_t)shadow;
    const uintptr_t end = start + sizeof(shadow);
    const uintptr_t address = (uintptr_t)source;

    if (source == NULL ||
        (address >= start && address <= end &&
         ((address - start) % sizeof(uint32_t) != 0U ||
          words > (end - address) / sizeof(uint32_t)))) {
        printf("[HPU][FAIL][%s] source=0x%lx words=%u invalid_source_range\n",
               operation, (unsigned long)address, words);
        return 1;
    }
    return 0;
}

static void modulus_record(uint32_t *record, uint32_t q) {
    /* floor(2^64/q)，避免直接构造溢出的 2^64；整除时补回 UINT64_MAX 少的 1。 */
    const uint64_t mu = UINT64_MAX / q + (UINT64_MAX % q == (uint64_t)q - 1U);

    record[0] = q;
    record[1] = (uint32_t)mu;
    record[2] = (uint32_t)(mu >> 32U);
    record[3] = 0U;
}

int v2_prepare(unsigned profile, uint32_t q0, uint32_t q1) {
    volatile uint32_t *memory;
    unsigned word;
    /* 同一轮只构造一次；放在系数循环内会反复产生栈写入。 */
    const uint32_t edge_a[] = {
        0U, 1U, q0 - 1U, q0 - 2U, 0U, 1U, q0 - 1U, q0 - 2U
    };
    const uint32_t edge_b[] = {
        0U, 1U, 1U, q0 - 1U, q0 - 1U, q0 - 2U, q0 - 1U, q0 - 2U
    };

    prepared = 0;
    /* q>=65537 保证 Barrett mu 可用 48 bit 表示；先验证再写 DDR。 */
    if (profile > 1U || q0 < UINT32_C(65537) || q1 < UINT32_C(65537)) {
        printf("[HPU][FAIL][prepare] profile=%u q0=%u q1=%u "
               "expected_profile=0/1 expected_q>=65537\n", profile, q0, q1);
        return 1;
    }
    for (word = 0U; word < WINDOW_WORDS; ++word)
        shadow[word] = guard_value(word);
    for (word = 0U; word < WINDOW_LINES; ++word)
        output_allowed[word] = 0U;

    for (word = 0U; word < POLY_WORDS; ++word) {
        uint32_t a;
        uint32_t b;

        if (profile == 0U) {
            /* producer 的规范输入无需再走硬件除法；较小 q 仍正确取模。 */
            a = RNS_A[word];
            b = RNS_B[word];
            if (a >= q0) a %= q0;
            if (b >= q0) b %= q0;
        } else {
            /* AM 边界变体：零/一/模数前沿、等值、借位与回绕交错出现。 */
            a = edge_a[word % 8U];
            b = edge_b[word % 8U];
        }
        shadow[LINE_A * WORDS_PER_LINE + word] = a;
        shadow[LINE_B * WORDS_PER_LINE + word] = b;
    }
    for (word = 0U; word < WORDS_PER_LINE; ++word)
        shadow[LINE_MOD * WORDS_PER_LINE + word] = 0U;
    modulus_record(&shadow[LINE_MOD * WORDS_PER_LINE], q0);
    modulus_record(&shadow[LINE_MOD * WORDS_PER_LINE + 6U * 4U], q1);

    memory = ddr_line(0U);
    for (word = 0U; word < WINDOW_WORDS; ++word)
        memory[word] = shadow[word];
    clean_lines(0U, WINDOW_LINES);
    prepared = 1;
    return 0;
}

const uint32_t *v2_expected(unsigned line) {
    if (word_range("expected", line, 1U) != 0) return NULL;
    return &shadow[line * WORDS_PER_LINE];
}

int v2_fill(unsigned line, uint32_t value, unsigned words) {
    volatile uint32_t *memory;
    unsigned word;

    if (word_range("fill", line, words) != 0) return 1;
    memory = ddr_line(line);
    for (word = 0U; word < words; ++word) {
        shadow[line * WORDS_PER_LINE + word] = value;
        memory[word] = value;
    }
    clean_lines(line, line_count(words));
    return 0;
}

int v2_copy(unsigned line, const uint32_t *data, unsigned words) {
    volatile uint32_t *memory;
    uint32_t *expected;
    unsigned word;

    if (word_range("copy", line, words) != 0) return 1;
    if (source_range("copy", data, words) != 0) return 1;
    expected = &shadow[line * WORDS_PER_LINE];
    /* 支持从 v2_expected() 返回的重叠区间复制，不边读边破坏源影子。 */
    memmove(expected, data, (size_t)words * sizeof(uint32_t));
    memory = ddr_line(line);
    for (word = 0U; word < words; ++word)
        memory[word] = expected[word];
    clean_lines(line, line_count(words));
    return 0;
}

int v2_allow_output(unsigned line, unsigned lines) {
    unsigned offset;

    if (!prepared || line >= WINDOW_LINES || lines == 0U ||
        lines > WINDOW_LINES - line) {
        printf("[HPU][FAIL][allow-output] line=%u lines=%u "
               "window_lines=%u prepared=%d\n", line, lines, WINDOW_LINES, prepared);
        return 1;
    }
    for (offset = 0U; offset < lines; ++offset)
        output_allowed[line + offset] = 1U;
    return 0;
}

int v2_check_memory(const char *phase) {
    volatile const uint32_t *memory;

    if (!prepared || phase == NULL) {
        printf("[HPU][FAIL][memory] prepared=%d phase_present=%u\n",
               prepared, phase != NULL ? 1U : 0U);
        return 1;
    }
    /* guard 只能证明本窗口未改写；窗口外/读事务副作用仍由 AXI monitor 验证。 */
    /* 只失效将被读取的连续只读段；输出由 result 检查失效，避免重复 CBO。 */
    for (unsigned line = 0U; line < WINDOW_LINES;) {
        if (output_allowed[line]) { ++line; continue; }
        const unsigned first = line;
        while (line < WINDOW_LINES && !output_allowed[line]) ++line;
        invalidate_lines(first, line - first);
    }
    memory = ddr_line(0U);
    /* permission 每 line 只查一次；每个非输出 word 仍逐项比较，不缩小 guard。 */
    for (unsigned line = 0U; line < WINDOW_LINES; ++line) {
        if (output_allowed[line] != 0U) continue;
        for (unsigned index = 0U; index < WORDS_PER_LINE; ++index) {
            const unsigned word = line * WORDS_PER_LINE + index;
            const uint32_t actual = memory[word];
            if (actual != shadow[word]) {
                printf("[HPU][FAIL][%s][readonly-or-guard] addr=0x%lx "
                       "line=%u index=%u actual=0x%x expected=0x%x\n",
                       phase, (unsigned long)(MEM_BASE + (uintptr_t)word * 4U),
                       line, index, actual, shadow[word]);
                return 1;
            }
        }
    }
    return 0;
}

int v2_check_words(const char *phase, unsigned line,
                   const uint32_t *golden, unsigned words, uint32_t q) {
    volatile const uint32_t *memory;

    if (word_range("check-words", line, words) != 0) return 1;
    if (source_range("check-words", golden, words) != 0) return 1;
    if (phase == NULL) {
        printf("[HPU][FAIL][check-words] line=%u words=%u "
               "phase_present=%u golden_present=%u\n", line, words,
               phase != NULL ? 1U : 0U, golden != NULL ? 1U : 0U);
        return 1;
    }
    invalidate_lines(line, line_count(words));
    memory = ddr_line(line);
    return result_compare(phase, memory, golden, words, q);
}
