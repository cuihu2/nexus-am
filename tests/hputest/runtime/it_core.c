#include "it_internal.h"

#define HPU_IT_DMA(word_, line_, count_)                                    \
    do {                                                                    \
        register uintptr_t line_reg __asm__("x10") = (line_);             \
        register uintptr_t count_reg __asm__("x11") = (count_);           \
        __asm__ volatile(".word %2"                                       \
                         :                                                  \
                         : "r"(line_reg), "r"(count_reg),                \
                           "i"((uint32_t)(word_))                          \
                         : "memory");                                     \
    } while (0)

#define HPU_IT_CUSTOM0(word_)                                               \
    __asm__ volatile(".word %0" : : "i"((uint32_t)(word_)) : "memory")

volatile uint32_t *hpu_it_line(unsigned line) {
    return hpu_line(line);
}

void hpu_it_clean_lines(unsigned first, unsigned lines) {
    hpu_cache_clean((uintptr_t)hpu_it_line(first),
                    (size_t)lines * HPU_LINE_BYTES);
}

void hpu_it_invalidate_lines(unsigned first, unsigned lines) {
    hpu_cache_invalidate((uintptr_t)hpu_it_line(first),
                         (size_t)lines * HPU_LINE_BYTES);
}

static uint32_t hpu_it_prng(uint32_t *state) {
    uint32_t value = *state != 0U ? *state : UINT32_C(0x48505549);
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

void hpu_it_fill_line(unsigned line, uint32_t seed, uint32_t modulus) {
    volatile uint32_t *destination = hpu_it_line(line);
    unsigned word;

    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word) {
        destination[word] = hpu_it_prng(&seed) % modulus;
    }
    hpu_fence();
}

void hpu_it_poison_line(unsigned line, uint32_t salt) {
    volatile uint32_t *destination = hpu_it_line(line);
    unsigned word;

    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word) {
        destination[word] = UINT32_C(0xd15ea5ed) ^ salt ^ word;
    }
    hpu_fence();
}

int hpu_it_compare_line(unsigned line, const uint32_t *expected,
                        unsigned words) {
    volatile const uint32_t *actual = hpu_it_line(line);
    unsigned word;

    if (expected == NULL || words > HPU_WORDS_PER_LINE) {
        return HPU_IT_ERR_ARGUMENT;
    }
    hpu_it_invalidate_lines(line, 1U);
    for (word = 0U; word < words; ++word) {
        if (actual[word] != expected[word]) return HPU_IT_ERR_COMPARE;
    }
    return HPU_IT_OK;
}

int hpu_it_compare_regions(unsigned actual_line, unsigned expected_line,
                           unsigned lines) {
    volatile const uint32_t *actual;
    volatile const uint32_t *expected;
    unsigned word;

    if (lines == 0U || lines > HPU_IT_WINDOW_LINES ||
        actual_line > HPU_IT_WINDOW_LINES - lines ||
        expected_line > HPU_IT_WINDOW_LINES - lines) {
        return HPU_IT_ERR_ARGUMENT;
    }
    hpu_it_invalidate_lines(actual_line, lines);
    actual = hpu_it_line(actual_line);
    expected = hpu_it_line(expected_line);
    for (word = 0U; word < lines * HPU_WORDS_PER_LINE; ++word) {
        if (actual[word] != expected[word]) return HPU_IT_ERR_COMPARE;
    }
    return HPU_IT_OK;
}

uint32_t hpu_it_mod_add(uint32_t a, uint32_t b, uint32_t modulus) {
    uint64_t sum = (uint64_t)(a % modulus) + (uint64_t)(b % modulus);
    return (uint32_t)(sum >= modulus ? sum - modulus : sum);
}

uint32_t hpu_it_mod_sub(uint32_t a, uint32_t b, uint32_t modulus) {
    uint32_t left = a % modulus;
    uint32_t right = b % modulus;
    return left >= right ? left - right : left + modulus - right;
}

uint32_t hpu_it_mod_mul(uint32_t a, uint32_t b, uint32_t modulus) {
    return (uint32_t)(((uint64_t)a * b) % modulus);
}

int hpu_it_wait_window_valid(int expected_valid) {
    unsigned timeout;

    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        uint32_t status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
        int valid = (status & HPU_STATUS_WINDOW_VALID) != 0U;
        if (valid == (expected_valid != 0)) return HPU_IT_OK;
    }
    return HPU_IT_ERR_WINDOW;
}

static void hpu_it_set_mod_record(volatile uint32_t *record,
                                  uint32_t modulus) {
    uint64_t mu = UINT64_MAX / modulus;
    record[0] = modulus;
    record[1] = (uint32_t)mu;
    record[2] = (uint32_t)(mu >> 32U) & UINT32_C(0xffff);
    record[3] = 0U;
}

int hpu_it_prepare_data(uint32_t seed) {
    volatile uint32_t *modulus = hpu_it_line(HPU_IT_LINE_MOD);
    volatile uint32_t *input_a = hpu_it_line(HPU_IT_LINE_SRC_A);
    volatile uint32_t *input_b = hpu_it_line(HPU_IT_LINE_SRC_B);
    unsigned word;

    for (word = 0U; word < HPU_IT_COEFFICIENTS; ++word) {
        if (hpu_rns_input_a[word] >= HPU_IT_Q0 ||
            hpu_rns_input_b[word] >= HPU_IT_Q0) {
            return HPU_IT_ERR_ARGUMENT;
        }
        input_a[word] = hpu_rns_input_a[word];
        input_b[word] = hpu_rns_input_b[word];
    }
    for (word = 0U; word < HPU_WORDS_PER_LINE; ++word) modulus[word] = 0U;
    hpu_it_set_mod_record(modulus, HPU_IT_Q0);
    hpu_it_set_mod_record(modulus + 6U * 4U, HPU_IT_Q1);
    hpu_it_fill_line(HPU_IT_LINE_TWIDDLE,
                     seed ^ UINT32_C(0x54), HPU_IT_Q0);
    for (word = 0U; word < HPU_IT_POLY_LINES; ++word) {
        hpu_it_poison_line(HPU_IT_LINE_OUT_A + word,
                           UINT32_C(0x4f410000) ^ word);
        hpu_it_poison_line(HPU_IT_LINE_OUT_B + word,
                           UINT32_C(0x4f420000) ^ word);
    }
    hpu_it_clean_lines(HPU_IT_LINE_MOD, 1U);
    hpu_it_clean_lines(HPU_IT_LINE_SRC_A, HPU_IT_POLY_LINES);
    hpu_it_clean_lines(HPU_IT_LINE_SRC_B, HPU_IT_POLY_LINES);
    hpu_it_clean_lines(HPU_IT_LINE_TWIDDLE, 1U);
    hpu_it_clean_lines(HPU_IT_LINE_OUT_A, HPU_IT_POLY_LINES);
    hpu_it_clean_lines(HPU_IT_LINE_OUT_B, HPU_IT_POLY_LINES);

    return HPU_IT_OK;
}

int hpu_it_check_final_status(void) {
    uint32_t status = hpu_csr_read32(HPU_CSR_STATUS_ADDR);
    uint32_t fault = hpu_csr_read32(HPU_CSR_FAULT_ADDR);

    if ((fault & HPU_FAULT_VALID) != 0U ||
        (status & HPU_STATUS_FAULT_VALID) != 0U) {
        return HPU_IT_ERR_FAULT;
    }
    if ((status & HPU_STATUS_WINDOW_VALID) == 0U ||
        (status & HPU_STATUS_BUSY) != 0U) {
        return HPU_IT_ERR_WINDOW;
    }
    return HPU_IT_OK;
}

int hpu_it_wait_irq_level(void) {
    unsigned timeout;

    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        if ((hpu_csr_read32(HPU_CSR_FAULT_ADDR) & HPU_FAULT_VALID) != 0U) {
            return HPU_IT_ERR_FAULT;
        }
        if ((hpu_csr_read32(HPU_CSR_IRQ_ADDR) & HPU_IRQ_LEVEL) != 0U)
            return HPU_IT_OK;
    }
    return HPU_IT_ERR_TIMEOUT;
}

void hpu_it_issue_psync(void) {
    HPU_IT_CUSTOM0(HPU_INSN_PSYNC);
}

int hpu_it_issue_dload(unsigned object, unsigned line, unsigned lines,
                       int modulus_table) {
    if (lines == 0U || line >= HPU_IT_WINDOW_LINES ||
        lines > HPU_IT_WINDOW_LINES - line) return HPU_IT_ERR_ARGUMENT;
    if (modulus_table) {
        if (object != 4U) return HPU_IT_ERR_ARGUMENT;
        HPU_IT_DMA(HPU_INSN_DLOAD_P4_MOD, line, lines);
        return HPU_IT_OK;
    }
    switch (object) {
    case 0U: HPU_IT_DMA(HPU_INSN_DLOAD_P0_POLY, line, lines); return 0;
    case 1U: HPU_IT_DMA(HPU_INSN_DLOAD_P1_POLY, line, lines); return 0;
#if defined(HPU_INSN_DLOAD_P2_POLY)
    case 2U: HPU_IT_DMA(HPU_INSN_DLOAD_P2_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P3_POLY)
    case 3U: HPU_IT_DMA(HPU_INSN_DLOAD_P3_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P4_POLY)
    case 4U: HPU_IT_DMA(HPU_INSN_DLOAD_P4_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P5_POLY)
    case 5U: HPU_IT_DMA(HPU_INSN_DLOAD_P5_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P6_POLY)
    case 6U: HPU_IT_DMA(HPU_INSN_DLOAD_P6_POLY, line, lines); return 0;
#endif
#if defined(HPU_INSN_DLOAD_P7_POLY)
    case 7U: HPU_IT_DMA(HPU_INSN_DLOAD_P7_POLY, line, lines); return 0;
#endif
    default: return HPU_IT_ERR_ENCODING_UNAVAILABLE;
    }
}

void hpu_it_issue_raw_dload_p0(uintptr_t line, uintptr_t lines) {
    HPU_IT_DMA(HPU_INSN_DLOAD_P0_POLY, line, lines);
}

int hpu_it_issue_dstore(unsigned object, unsigned line, unsigned lines,
                        int release) {
    if (lines == 0U || line >= HPU_IT_WINDOW_LINES ||
        lines > HPU_IT_WINDOW_LINES - line) return HPU_IT_ERR_ARGUMENT;
    if (!release) {
#if defined(HPU_INSN_DSTORE_P0_KEEP)
        if (object == 0U) {
            HPU_IT_DMA(HPU_INSN_DSTORE_P0_KEEP, line, lines);
            return HPU_IT_OK;
        }
#endif
#if defined(HPU_INSN_DSTORE_P2_KEEP)
        if (object == 2U) {
            HPU_IT_DMA(HPU_INSN_DSTORE_P2_KEEP, line, lines);
            return HPU_IT_OK;
        }
#endif
#if defined(HPU_INSN_DSTORE_P7_KEEP)
        if (object == 7U) {
            HPU_IT_DMA(HPU_INSN_DSTORE_P7_KEEP, line, lines);
            return HPU_IT_OK;
        }
#endif
        return HPU_IT_ERR_ENCODING_UNAVAILABLE;
    }
    switch (object) {
    case 0U: HPU_IT_DMA(HPU_INSN_DSTORE_P0_RELEASE, line, lines); return 0;
#if defined(HPU_INSN_DSTORE_P1_RELEASE)
    case 1U: HPU_IT_DMA(HPU_INSN_DSTORE_P1_RELEASE, line, lines); return 0;
#endif
    case 2U: HPU_IT_DMA(HPU_INSN_DSTORE_P2_RELEASE, line, lines); return 0;
#if defined(HPU_INSN_DSTORE_P3_RELEASE)
    case 3U: HPU_IT_DMA(HPU_INSN_DSTORE_P3_RELEASE, line, lines); return 0;
#endif
#if defined(HPU_INSN_DSTORE_P4_RELEASE)
    case 4U: HPU_IT_DMA(HPU_INSN_DSTORE_P4_RELEASE, line, lines); return 0;
#endif
#if defined(HPU_INSN_DSTORE_P5_RELEASE)
    case 5U: HPU_IT_DMA(HPU_INSN_DSTORE_P5_RELEASE, line, lines); return 0;
#endif
#if defined(HPU_INSN_DSTORE_P6_RELEASE)
    case 6U: HPU_IT_DMA(HPU_INSN_DSTORE_P6_RELEASE, line, lines); return 0;
#endif
#if defined(HPU_INSN_DSTORE_P7_RELEASE)
    case 7U: HPU_IT_DMA(HPU_INSN_DSTORE_P7_RELEASE, line, lines); return 0;
#endif
    default: return HPU_IT_ERR_ENCODING_UNAVAILABLE;
    }
}

int hpu_it_issue_pmodld(unsigned context) {
    switch (context) {
    case 0U: HPU_IT_CUSTOM0(HPU_INSN_PMODLD_0); return 0;
#if defined(HPU_INSN_PMODLD_1)
    case 1U: HPU_IT_CUSTOM0(HPU_INSN_PMODLD_1); return 0;
#endif
#if defined(HPU_INSN_PMODLD_2)
    case 2U: HPU_IT_CUSTOM0(HPU_INSN_PMODLD_2); return 0;
#endif
#if defined(HPU_INSN_PMODLD_3)
    case 3U: HPU_IT_CUSTOM0(HPU_INSN_PMODLD_3); return 0;
#endif
#if defined(HPU_INSN_PMODLD_4)
    case 4U: HPU_IT_CUSTOM0(HPU_INSN_PMODLD_4); return 0;
#endif
#if defined(HPU_INSN_PMODLD_5)
    case 5U: HPU_IT_CUSTOM0(HPU_INSN_PMODLD_5); return 0;
#endif
#if defined(HPU_INSN_PMODLD_6)
    case 6U: HPU_IT_CUSTOM0(HPU_INSN_PMODLD_6); return 0;
#endif
    default: return HPU_IT_ERR_ENCODING_UNAVAILABLE;
    }
}

int hpu_it_issue_arith(hpu_it_arith_op operation) {
    switch (operation) {
    case HPU_IT_ARITH_PADD:
        HPU_IT_CUSTOM0(HPU_INSN_PADD_P2_P0_P1);
        return HPU_IT_OK;
#if defined(HPU_INSN_PSUB_P2_P0_P1)
    case HPU_IT_ARITH_PSUB:
        HPU_IT_CUSTOM0(HPU_INSN_PSUB_P2_P0_P1);
        return HPU_IT_OK;
#endif
#if defined(HPU_INSN_PMUL_P2_P0_P1)
    case HPU_IT_ARITH_PMUL:
        HPU_IT_CUSTOM0(HPU_INSN_PMUL_P2_P0_P1);
        return HPU_IT_OK;
#endif
#if defined(HPU_INSN_PMAC_P2_P0_P1)
    case HPU_IT_ARITH_PMAC:
        HPU_IT_CUSTOM0(HPU_INSN_PMAC_P2_P0_P1);
        return HPU_IT_OK;
#endif
    default: return HPU_IT_ERR_ENCODING_UNAVAILABLE;
    }
}

int hpu_it_issue_pmac_immediate(unsigned immediate) {
    switch (immediate) {
#if defined(HPU_INSN_PMAC_IMM0_P2_P0)
    case 0U: HPU_IT_CUSTOM0(HPU_INSN_PMAC_IMM0_P2_P0); return 0;
#endif
#if defined(HPU_INSN_PMAC_IMM1_P2_P0)
    case 1U: HPU_IT_CUSTOM0(HPU_INSN_PMAC_IMM1_P2_P0); return 0;
#endif
#if defined(HPU_INSN_PMAC_IMM255_P2_P0)
    case 255U: HPU_IT_CUSTOM0(HPU_INSN_PMAC_IMM255_P2_P0); return 0;
#endif
    default: return HPU_IT_ERR_ENCODING_UNAVAILABLE;
    }
}

#define HPU_IT_STAGE_CASE(number_)                                           \
    case number_: HPU_IT_CUSTOM0(HPU_IT_STAGE_WORD(number_)); return 0

int hpu_it_issue_transform(int inverse, unsigned stage) {
    if (stage > 11U) return HPU_IT_ERR_ARGUMENT;
    if (inverse) {
#if defined(HPU_INSN_PINTT_STAGE0)
#define HPU_IT_STAGE_WORD(number_) HPU_INSN_PINTT_STAGE##number_
        switch (stage) {
        HPU_IT_STAGE_CASE(0); HPU_IT_STAGE_CASE(1); HPU_IT_STAGE_CASE(2);
        HPU_IT_STAGE_CASE(3); HPU_IT_STAGE_CASE(4); HPU_IT_STAGE_CASE(5);
        HPU_IT_STAGE_CASE(6); HPU_IT_STAGE_CASE(7); HPU_IT_STAGE_CASE(8);
        HPU_IT_STAGE_CASE(9); HPU_IT_STAGE_CASE(10); HPU_IT_STAGE_CASE(11);
        default: break;
        }
#undef HPU_IT_STAGE_WORD
#endif
    } else {
#if defined(HPU_INSN_PNTT_STAGE0)
#define HPU_IT_STAGE_WORD(number_) HPU_INSN_PNTT_STAGE##number_
        switch (stage) {
        HPU_IT_STAGE_CASE(0); HPU_IT_STAGE_CASE(1); HPU_IT_STAGE_CASE(2);
        HPU_IT_STAGE_CASE(3); HPU_IT_STAGE_CASE(4); HPU_IT_STAGE_CASE(5);
        HPU_IT_STAGE_CASE(6); HPU_IT_STAGE_CASE(7); HPU_IT_STAGE_CASE(8);
        HPU_IT_STAGE_CASE(9); HPU_IT_STAGE_CASE(10); HPU_IT_STAGE_CASE(11);
        default: break;
        }
#undef HPU_IT_STAGE_WORD
#endif
    }
    return HPU_IT_ERR_ENCODING_UNAVAILABLE;
}

int hpu_it_issue_pfree(unsigned object) {
    switch (object) {
#if defined(HPU_INSN_PFREE_P0)
    case 0U: HPU_IT_CUSTOM0(HPU_INSN_PFREE_P0); return 0;
    case 1U: HPU_IT_CUSTOM0(HPU_INSN_PFREE_P1); return 0;
    case 2U: HPU_IT_CUSTOM0(HPU_INSN_PFREE_P2); return 0;
    case 3U: HPU_IT_CUSTOM0(HPU_INSN_PFREE_P3); return 0;
    case 4U: HPU_IT_CUSTOM0(HPU_INSN_PFREE_P4); return 0;
    case 5U: HPU_IT_CUSTOM0(HPU_INSN_PFREE_P5); return 0;
    case 6U: HPU_IT_CUSTOM0(HPU_INSN_PFREE_P6); return 0;
    case 7U: HPU_IT_CUSTOM0(HPU_INSN_PFREE_P7); return 0;
#endif
    default: return HPU_IT_ERR_ENCODING_UNAVAILABLE;
    }
}

int hpu_it_not_issued(void) {
    unsigned word;

    /* Keep both producer fixtures in fail-closed algorithm ELFs as promised. */
    for (word = 0U; word < HPU_IT_COEFFICIENTS; ++word) {
        if (hpu_rns_input_a[word] >= HPU_IT_Q0 ||
            hpu_rns_input_b[word] >= HPU_IT_Q0) {
            return HPU_IT_ERR_ARGUMENT;
        }
    }
    return HPU_IT_ERR_NOT_ISSUED;
}
