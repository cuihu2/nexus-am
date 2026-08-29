#include "it_internal.h"

#define DMA(word_, line_, count_)                                          \
    do {                                                                    \
        register uintptr_t line_reg __asm__("x10") = (line_);             \
        register uintptr_t count_reg __asm__("x11") = (count_);           \
        __asm__ volatile(".word %2"                                       \
                         :                                                  \
                         : "r"(line_reg), "r"(count_reg),                \
                           "i"((uint32_t)(word_))                          \
                         : "memory");                                     \
    } while (0)

#define CUSTOM0(word_)                                                     \
    __asm__ volatile(".word %0" : : "i"((uint32_t)(word_)) : "memory")

int expect_csr(uintptr_t address, uint32_t expected, uint32_t mask) {
    return (hpu_csr_read32(address) & mask) == (expected & mask) ? 0 : 1;
}

volatile uint32_t *ddr_line(unsigned line) {
    return hpu_line(line);
}

void clean_lines(unsigned first, unsigned lines) {
    hpu_cache_clean((uintptr_t)ddr_line(first),
                    (size_t)lines * HPU_LINE_BYTES);
}

void invalidate_lines(unsigned first, unsigned lines) {
    hpu_cache_invalidate((uintptr_t)ddr_line(first),
                         (size_t)lines * HPU_LINE_BYTES);
}

static uint32_t step_prng(uint32_t *state) {
    uint32_t value = *state != 0U ? *state : UINT32_C(0x48505549);
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

void fill_line(unsigned line, uint32_t seed, uint32_t modulus) {
    volatile uint32_t *destination = ddr_line(line);
    unsigned word;

    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word) {
        destination[word] = step_prng(&seed) % modulus;
    }
    hpu_fence();
}

void poison_line(unsigned line, uint32_t salt) {
    volatile uint32_t *destination = ddr_line(line);
    unsigned word;

    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word) {
        destination[word] = UINT32_C(0xd15ea5ed) ^ salt ^ word;
    }
    hpu_fence();
}

int check_line(unsigned line, const uint32_t *expected, unsigned words) {
    volatile const uint32_t *actual = ddr_line(line);
    unsigned word;

    if (expected == NULL || words > HPU_WORDS_PER_LINE) {
        return STEP_ERR_ARGUMENT;
    }
    invalidate_lines(line, 1U);
    for (word = 0U; word < words; ++word) {
        if (actual[word] != expected[word]) return STEP_ERR_COMPARE;
    }
    return STEP_OK;
}

int check_regions(unsigned actual_line, unsigned expected_line,
                  unsigned lines) {
    volatile const uint32_t *actual;
    volatile const uint32_t *expected;
    unsigned word;

    if (lines == 0U || lines > WINDOW_LINES ||
        actual_line > WINDOW_LINES - lines ||
        expected_line > WINDOW_LINES - lines) {
        return STEP_ERR_ARGUMENT;
    }
    invalidate_lines(actual_line, lines);
    actual = ddr_line(actual_line);
    expected = ddr_line(expected_line);
    for (word = 0U; word < lines * HPU_WORDS_PER_LINE; ++word) {
        if (actual[word] != expected[word]) return STEP_ERR_COMPARE;
    }
    return STEP_OK;
}

uint32_t mod_add(uint32_t a, uint32_t b, uint32_t modulus) {
    uint64_t sum = (uint64_t)(a % modulus) + (uint64_t)(b % modulus);
    return (uint32_t)(sum >= modulus ? sum - modulus : sum);
}

uint32_t mod_sub(uint32_t a, uint32_t b, uint32_t modulus) {
    uint32_t left = a % modulus;
    uint32_t right = b % modulus;
    return left >= right ? left - right : left + modulus - right;
}

uint32_t mod_mul(uint32_t a, uint32_t b, uint32_t modulus) {
    return (uint32_t)(((uint64_t)a * b) % modulus);
}

int wait_window(int expected_valid) {
    unsigned timeout;

    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        uint32_t status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
        int valid = (status & HPU_STATUS_WINDOW_VALID) != 0U;
        if (valid == (expected_valid != 0)) return STEP_OK;
    }
    return STEP_ERR_WINDOW;
}

static void set_mod_record(volatile uint32_t *record, uint32_t modulus) {
    uint64_t mu = UINT64_MAX / modulus;
    record[0] = modulus;
    record[1] = (uint32_t)mu;
    record[2] = (uint32_t)(mu >> 32U) & UINT32_C(0xffff);
    record[3] = 0U;
}

int prepare_data(uint32_t seed) {
    volatile uint32_t *modulus = ddr_line(LINE_MOD);
    volatile uint32_t *input_a = ddr_line(LINE_A);
    volatile uint32_t *input_b = ddr_line(LINE_B);
    unsigned word;

    for (word = 0U; word < POLY_WORDS; ++word) {
        if (hpu_rns_input_a[word] >= MOD_Q0 ||
            hpu_rns_input_b[word] >= MOD_Q0) {
            return STEP_ERR_ARGUMENT;
        }
        input_a[word] = hpu_rns_input_a[word];
        input_b[word] = hpu_rns_input_b[word];
    }
    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word) modulus[word] = 0U;
    set_mod_record(modulus, MOD_Q0);
    set_mod_record(modulus + 6U * 4U, MOD_Q1);
    fill_line(LINE_TWIDDLE, seed ^ UINT32_C(0x54), MOD_Q0);
    for (word = 0U; word < POLY_LINES; ++word) {
        poison_line(LINE_OUT + word, UINT32_C(0x4f410000) ^ word);
        poison_line(LINE_OUT_B + word, UINT32_C(0x4f420000) ^ word);
    }
    clean_lines(LINE_MOD, 1U);
    clean_lines(LINE_A, POLY_LINES);
    clean_lines(LINE_B, POLY_LINES);
    clean_lines(LINE_TWIDDLE, 1U);
    clean_lines(LINE_OUT, POLY_LINES);
    clean_lines(LINE_OUT_B, POLY_LINES);

    return STEP_OK;
}

int check_status(void) {
    uint32_t status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    uint32_t fault = hpu_csr_read32(HPU_CSR_FAULT_ADDR);

    if ((fault & HPU_FAULT_VALID) != 0U ||
        (status & HPU_STATUS_FAULT_VALID) != 0U) {
        return STEP_ERR_FAULT;
    }
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U ||
        (status & HPU_STATUS_BUSY) != 0U) {
        return STEP_ERR_WINDOW;
    }
    return STEP_OK;
}

int wait_irq(void) {
    unsigned timeout;

    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U) {
            return STEP_ERR_FAULT;
        }
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) != 0U)
            return STEP_OK;
    }
    return STEP_ERR_TIMEOUT;
}

void psync(void) {
    CUSTOM0(HPU_INSN_PSYNC);
}

static int issue_dload(unsigned object, unsigned line, unsigned lines,
                       int modulus_table) {
    if (lines == 0U || line >= WINDOW_LINES ||
        lines > WINDOW_LINES - line) return STEP_ERR_ARGUMENT;
    if (modulus_table) {
        if (object != 4U) return STEP_ERR_ARGUMENT;
        DMA(HPU_INSN_DLOAD_P4_MOD, line, lines);
        return STEP_OK;
    }
    switch (object) {
    case 0U: DMA(HPU_INSN_DLOAD_P0_POLY, line, lines); return 0;
    case 1U: DMA(HPU_INSN_DLOAD_P1_POLY, line, lines); return 0;
#if defined(HPU_INSN_DLOAD_P2_POLY)
    case 2U: DMA(HPU_INSN_DLOAD_P2_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P3_POLY)
    case 3U: DMA(HPU_INSN_DLOAD_P3_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P4_POLY)
    case 4U: DMA(HPU_INSN_DLOAD_P4_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P5_POLY)
    case 5U: DMA(HPU_INSN_DLOAD_P5_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P6_POLY)
    case 6U: DMA(HPU_INSN_DLOAD_P6_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P7_POLY)
    case 7U: DMA(HPU_INSN_DLOAD_P7_POLY, line, lines); return 0;
#endif
    default: return STEP_ERR_ENCODING_UNAVAILABLE;
    }
}

int dload(unsigned object, unsigned line, unsigned lines) {
    return issue_dload(object, line, lines, 0);
}

int dload_mod(unsigned line, unsigned lines) {
    return issue_dload(P4, line, lines, 1);
}

void raw_dload_p0(uintptr_t line, uintptr_t lines) {
    DMA(HPU_INSN_DLOAD_P0_POLY, line, lines);
}

static int issue_dstore(unsigned object, unsigned line, unsigned lines,
                        int release) {
    if (lines == 0U || line >= WINDOW_LINES ||
        lines > WINDOW_LINES - line) return STEP_ERR_ARGUMENT;
    if (!release) {
#if defined(HPU_INSN_DSTORE_P0_KEEP)
        if (object == 0U) {
            DMA(HPU_INSN_DSTORE_P0_KEEP, line, lines);
            return STEP_OK;
        }
#endif
#if defined(HPU_INSN_DSTORE_P2_KEEP)
        if (object == 2U) {
            DMA(HPU_INSN_DSTORE_P2_KEEP, line, lines);
            return STEP_OK;
        }
#endif
#if defined(HPU_INSN_DSTORE_P7_KEEP)
        if (object == 7U) {
            DMA(HPU_INSN_DSTORE_P7_KEEP, line, lines);
            return STEP_OK;
        }
#endif
        return STEP_ERR_ENCODING_UNAVAILABLE;
    }
    switch (object) {
    case 0U: DMA(HPU_INSN_DSTORE_P0_RELEASE, line, lines); return 0;
#if defined(HPU_INSN_DSTORE_P1_RELEASE)
    case 1U: DMA(HPU_INSN_DSTORE_P1_RELEASE, line, lines); return 0;
#endif
    case 2U: DMA(HPU_INSN_DSTORE_P2_RELEASE, line, lines); return 0;
#if defined(HPU_INSN_DSTORE_P3_RELEASE)
    case 3U: DMA(HPU_INSN_DSTORE_P3_RELEASE, line, lines); return 0;
#endif
#if defined(HPU_INSN_DSTORE_P4_RELEASE)
    case 4U: DMA(HPU_INSN_DSTORE_P4_RELEASE, line, lines); return 0;
#endif
#if defined(HPU_INSN_DSTORE_P5_RELEASE)
    case 5U: DMA(HPU_INSN_DSTORE_P5_RELEASE, line, lines); return 0;
#endif
#if defined(HPU_INSN_DSTORE_P6_RELEASE)
    case 6U: DMA(HPU_INSN_DSTORE_P6_RELEASE, line, lines); return 0;
#endif
#if defined(HPU_INSN_DSTORE_P7_RELEASE)
    case 7U: DMA(HPU_INSN_DSTORE_P7_RELEASE, line, lines); return 0;
#endif
    default: return STEP_ERR_ENCODING_UNAVAILABLE;
    }
}

int dstore_release(unsigned object, unsigned line, unsigned lines) {
    return issue_dstore(object, line, lines, 1);
}

int dstore_keep(unsigned object, unsigned line, unsigned lines) {
    return issue_dstore(object, line, lines, 0);
}

int pmodld(unsigned context) {
    switch (context) {
    case 0U: CUSTOM0(HPU_INSN_PMODLD_0); return 0;
#if defined(HPU_INSN_PMODLD_1)
    case 1U: CUSTOM0(HPU_INSN_PMODLD_1); return 0;
#endif
#if defined(HPU_INSN_PMODLD_2)
    case 2U: CUSTOM0(HPU_INSN_PMODLD_2); return 0;
#endif
#if defined(HPU_INSN_PMODLD_3)
    case 3U: CUSTOM0(HPU_INSN_PMODLD_3); return 0;
#endif
#if defined(HPU_INSN_PMODLD_4)
    case 4U: CUSTOM0(HPU_INSN_PMODLD_4); return 0;
#endif
#if defined(HPU_INSN_PMODLD_5)
    case 5U: CUSTOM0(HPU_INSN_PMODLD_5); return 0;
#endif
#if defined(HPU_INSN_PMODLD_6)
    case 6U: CUSTOM0(HPU_INSN_PMODLD_6); return 0;
#endif
    default: return STEP_ERR_ENCODING_UNAVAILABLE;
    }
}

int padd(void) {
    CUSTOM0(HPU_INSN_PADD_P2_P0_P1);
    return STEP_OK;
}

int psub(void) {
#if defined(HPU_INSN_PSUB_P2_P0_P1)
    CUSTOM0(HPU_INSN_PSUB_P2_P0_P1);
    return STEP_OK;
#else
    return STEP_ERR_ENCODING_UNAVAILABLE;
#endif
}

int pmul(void) {
#if defined(HPU_INSN_PMUL_P2_P0_P1)
    CUSTOM0(HPU_INSN_PMUL_P2_P0_P1);
    return STEP_OK;
#else
    return STEP_ERR_ENCODING_UNAVAILABLE;
#endif
}

int pmac(void) {
#if defined(HPU_INSN_PMAC_P2_P0_P1)
    CUSTOM0(HPU_INSN_PMAC_P2_P0_P1);
    return STEP_OK;
#else
    return STEP_ERR_ENCODING_UNAVAILABLE;
#endif
}

int pmac_imm(unsigned immediate) {
    switch (immediate) {
#if defined(HPU_INSN_PMAC_IMM0_P2_P0)
    case 0U: CUSTOM0(HPU_INSN_PMAC_IMM0_P2_P0); return 0;
#endif
#if defined(HPU_INSN_PMAC_IMM1_P2_P0)
    case 1U: CUSTOM0(HPU_INSN_PMAC_IMM1_P2_P0); return 0;
#endif
#if defined(HPU_INSN_PMAC_IMM255_P2_P0)
    case 255U: CUSTOM0(HPU_INSN_PMAC_IMM255_P2_P0); return 0;
#endif
    default: return STEP_ERR_ENCODING_UNAVAILABLE;
    }
}

#define STAGE_CASE(number_)                                                \
    case number_: CUSTOM0(STAGE_WORD(number_)); return 0

static int issue_transform(int inverse, unsigned stage) {
    if (stage > 11U) return STEP_ERR_ARGUMENT;
    if (inverse) {
#if defined(HPU_INSN_PINTT_STAGE0)
#define STAGE_WORD(number_) HPU_INSN_PINTT_STAGE##number_
        switch (stage) {
        STAGE_CASE(0); STAGE_CASE(1); STAGE_CASE(2);
        STAGE_CASE(3); STAGE_CASE(4); STAGE_CASE(5);
        STAGE_CASE(6); STAGE_CASE(7); STAGE_CASE(8);
        STAGE_CASE(9); STAGE_CASE(10); STAGE_CASE(11);
        default: break;
        }
#undef STAGE_WORD
#endif
    } else {
#if defined(HPU_INSN_PNTT_STAGE0)
#define STAGE_WORD(number_) HPU_INSN_PNTT_STAGE##number_
        switch (stage) {
        STAGE_CASE(0); STAGE_CASE(1); STAGE_CASE(2);
        STAGE_CASE(3); STAGE_CASE(4); STAGE_CASE(5);
        STAGE_CASE(6); STAGE_CASE(7); STAGE_CASE(8);
        STAGE_CASE(9); STAGE_CASE(10); STAGE_CASE(11);
        default: break;
        }
#undef STAGE_WORD
#endif
    }
    return STEP_ERR_ENCODING_UNAVAILABLE;
}

int pntt_stage(unsigned stage) {
    return issue_transform(0, stage);
}

int pintt_stage(unsigned stage) {
    return issue_transform(1, stage);
}

int pfree(unsigned object) {
    switch (object) {
#if defined(HPU_INSN_PFREE_P0)
    case 0U: CUSTOM0(HPU_INSN_PFREE_P0); return 0;
    case 1U: CUSTOM0(HPU_INSN_PFREE_P1); return 0;
    case 2U: CUSTOM0(HPU_INSN_PFREE_P2); return 0;
    case 3U: CUSTOM0(HPU_INSN_PFREE_P3); return 0;
    case 4U: CUSTOM0(HPU_INSN_PFREE_P4); return 0;
    case 5U: CUSTOM0(HPU_INSN_PFREE_P5); return 0;
    case 6U: CUSTOM0(HPU_INSN_PFREE_P6); return 0;
    case 7U: CUSTOM0(HPU_INSN_PFREE_P7); return 0;
#endif
    default: return STEP_ERR_ENCODING_UNAVAILABLE;
    }
}

int not_issued(void) {
    unsigned word;

    /* Keep both producer fixtures in fail-closed algorithm ELFs as promised. */
    for (word = 0U; word < POLY_WORDS; ++word) {
        if (hpu_rns_input_a[word] >= MOD_Q0 ||
            hpu_rns_input_b[word] >= MOD_Q0) {
            return STEP_ERR_ARGUMENT;
        }
    }
    return STEP_ERR_NOT_ISSUED;
}
