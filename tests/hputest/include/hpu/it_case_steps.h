#ifndef HPU_IT_CASE_STEPS_H
#define HPU_IT_CASE_STEPS_H

#include <hpu/csr.h>
#include <stddef.h>
#include <stdint.h>

/* Shared DDR layout used by the migrated, producer-backed IT cases. */
enum {
    HPU_IT_LINE_SRC_A = 0,
    HPU_IT_LINE_SRC_B = 64,
    HPU_IT_LINE_OUT_A = 128,
    HPU_IT_LINE_MOD = 192,
    HPU_IT_LINE_OUT_B = 256,
    HPU_IT_LINE_SCRATCH = 320,
    HPU_IT_LINE_TWIDDLE = 384,
    HPU_IT_POLY_LINES = 64,
    HPU_IT_TEST_LINES = HPU_IT_POLY_LINES,
    HPU_IT_WINDOW_LINES = 512,
    HPU_IT_Q0 = 50061313,
    HPU_IT_Q1 = 50077697
};

#define HPU_IT_COEFFICIENTS (HPU_IT_POLY_LINES * HPU_WORDS_PER_LINE)
#define HPU_IT_MEM_BASE ((uintptr_t)HPU_MEM_BASE)

typedef enum {
    HPU_IT_ARITH_PADD,
    HPU_IT_ARITH_PSUB,
    HPU_IT_ARITH_PMUL,
    HPU_IT_ARITH_PMAC
} hpu_it_arith_op;

enum {
    HPU_IT_P0 = 0,
    HPU_IT_P1 = 1,
    HPU_IT_P2 = 2,
    HPU_IT_P3 = 3,
    HPU_IT_P4 = 4,
    HPU_IT_P5 = 5,
    HPU_IT_P6 = 6,
    HPU_IT_P7 = 7,
    HPU_IT_DMA_POLY = 0,
    HPU_IT_DMA_MOD_TABLE = 1,
    HPU_IT_STORE_KEEP = 0,
    HPU_IT_STORE_RELEASE = 1,
    HPU_IT_TRANSFORM_FORWARD = 0,
    HPU_IT_TRANSFORM_INVERSE = 1
};

/* Address-granularity CSR steps: keep every register visible in main(). */
static inline void hpu_it_csr_write32(uintptr_t address, uint32_t value) {
    hpu_csr_write32(address, value);
}

static inline uint32_t hpu_it_csr_read32(uintptr_t address) {
    return hpu_csr_read32(address);
}

static inline int hpu_it_csr_expect32(uintptr_t address, uint32_t expected,
                                      uint32_t mask) {
    return (hpu_csr_read32(address) & mask) == (expected & mask) ? 0 : 1;
}

/* Data-only setup.  This function never writes an HPU CSR or issues HPU work. */
int hpu_it_prepare_data(uint32_t seed);
volatile uint32_t *hpu_it_line(unsigned line);
void hpu_it_clean_lines(unsigned first, unsigned lines);
void hpu_it_invalidate_lines(unsigned first, unsigned lines);
void hpu_it_fill_line(unsigned line, uint32_t seed, uint32_t modulus);
void hpu_it_poison_line(unsigned line, uint32_t salt);
int hpu_it_compare_line(unsigned line, const uint32_t *expected,
                        unsigned words);
int hpu_it_wait_window_valid(int expected_valid);
int hpu_it_check_final_status(void);

/* Producer-encoded instruction steps. */
int hpu_it_issue_dload(unsigned object, unsigned line, unsigned lines,
                       int modulus_table);
void hpu_it_issue_raw_dload_p0(uintptr_t line, uintptr_t lines);
int hpu_it_issue_dstore(unsigned object, unsigned line, unsigned lines,
                        int release);
int hpu_it_issue_pmodld(unsigned context);
int hpu_it_issue_arith(hpu_it_arith_op operation);
int hpu_it_issue_pmac_immediate(unsigned immediate);
int hpu_it_issue_transform(int inverse, unsigned stage);
int hpu_it_issue_pfree(unsigned object);
void hpu_it_issue_psync(void);
int hpu_it_wait_irq_level(void);

/* Reference-model and comparison steps; none of these issue HPU commands. */
int hpu_it_prepare_accumulator(uint32_t seed, unsigned lines);
int hpu_it_compare_regions(unsigned actual_line, unsigned expected_line,
                           unsigned lines);
int hpu_it_compare_binary_output(hpu_it_arith_op operation,
                                 unsigned output_line, unsigned lines);
int hpu_it_compare_pmac_output(unsigned output_line, unsigned lines);

/* Keeps producer A/B referenced by a deliberately blocked testcase ELF. */
int hpu_it_not_issued(void);

#endif
