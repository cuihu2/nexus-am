#ifndef HPU_TEST_STEPS_H
#define HPU_TEST_STEPS_H

#include <hpu/csr.h>
#include <stddef.h>
#include <stdint.h>

/* DDR layout shared by the producer-backed tests (one line = 256 bytes). */
enum {
    LINE_OUT_B = 256,
    LINE_SCRATCH = 320,
    LINE_TWIDDLE = 384,
    WINDOW_LINES = 512,
    MOD_Q1 = 50077697
};

#define POLY_LINES RNS_LINES
#define POLY_WORDS RNS_COEFFICIENTS
#define MOD_Q0     MODULUS

/* HPU physical-object numbers used directly by DLOAD/DSTORE/PFREE. */
enum {
    P0 = 0,
    P1 = 1,
    P2 = 2,
    P3 = 3,
    P4 = 4,
    P5 = 5,
    P6 = 6,
    P7 = 7
};

/* MMIO/data setup.  prepare_data() never writes a CSR or issues HPU work. */
int expect_csr(uintptr_t address, uint32_t expected, uint32_t mask);
int prepare_data(uint32_t seed);
volatile uint32_t *ddr_line(unsigned line);
void clean_lines(unsigned first, unsigned lines);
void invalidate_lines(unsigned first, unsigned lines);
void fill_line(unsigned line, uint32_t seed, uint32_t modulus);
void poison_line(unsigned line, uint32_t salt);
int check_line(unsigned line, const uint32_t *expected, unsigned words);
int wait_window(int expected_valid);
int check_status(void);
int wait_irq(void);

/* One visible HPU instruction per call; words come from inline-asm output. */
int dload(unsigned object, unsigned line, unsigned lines);
int dload_mod(unsigned line, unsigned lines);
void raw_dload_p0(uintptr_t line, uintptr_t lines);
int dstore_release(unsigned object, unsigned line, unsigned lines);
int dstore_keep(unsigned object, unsigned line, unsigned lines);
int pmodld(unsigned context);
int padd(void);
int psub(void);
int pmul(void);
int pmac(void);
int pmac_imm(unsigned immediate);
int pntt_stage(unsigned stage);
int pintt_stage(unsigned stage);
int pfree(unsigned object);
void psync(void);

/* C reference checks; none of these issue HPU commands. */
int prepare_accumulator(uint32_t seed, unsigned lines);
int check_regions(unsigned actual_line, unsigned expected_line,
                  unsigned lines);
int check_padd(unsigned output_line, unsigned lines);
int check_psub(unsigned output_line, unsigned lines);
int check_pmul_result(unsigned output_line, unsigned lines);
int check_pmac(unsigned output_line, unsigned lines);

/* Keeps producer A/B in a deliberately blocked testcase ELF. */
int not_issued(void);

#endif
