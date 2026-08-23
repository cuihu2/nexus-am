#ifndef HPU_INLINE_ASM_H
#define HPU_INLINE_ASM_H

#include <stdint.h>

/*
 * HPU instruction adapter for the ordinary GNU RISC-V assembler.
 *
 * The upstream inline-asm encoder intentionally does not require GNU as to
 * know the HPU mnemonics.  Keep the instruction words here in lock-step with
 * third_party/inline-asm/encode and emit them with .word.  x10 carries the
 * custom1 line offset.  x11 is the nonzero line count for both DLOAD and
 * DSTORE; a zero count is an architectural fault.
 */

typedef struct {
    uintptr_t line_offset;
    uintptr_t line_count;
} hpu_dma_span;

#if defined(__riscv)
#define HPU_WORD(word_)                                                        \
    __asm__ volatile(".word %0" : : "i"((uint32_t)(word_)) : "memory")

#define HPU_DMA_WORD(word_, span_)                                             \
    do {                                                                       \
        register uintptr_t hpu_rs1 __asm__("x10") = (span_).line_offset;      \
        register uintptr_t hpu_rs2 __asm__("x11") = (span_).line_count;       \
        __asm__ volatile(                                                      \
            ".word %2"                                                        \
            :                                                                  \
            : "r"(hpu_rs1), "r"(hpu_rs2), "i"((uint32_t)(word_))            \
            : "memory");                                                      \
    } while (0)
#else
#define HPU_WORD(word_) do { (void)(word_); } while (0)
#define HPU_DMA_WORD(word_, span_)                                             \
    do { (void)(word_); (void)(span_); } while (0)
#endif

static inline int hpu_target_available(void) {
#if defined(__riscv)
    return 1;
#else
    return 0;
#endif
}

/* custom1 encodings use x10/x11. */
#define HPU_DEFINE_DLOAD(name_, word_)                                         \
    static inline void name_(hpu_dma_span span) { HPU_DMA_WORD(word_, span); }
#define HPU_DEFINE_DSTORE(name_, word_)                                        \
    static inline void name_(hpu_dma_span span) { HPU_DMA_WORD(word_, span); }

HPU_DEFINE_DLOAD(hpu_dload_mod_p4,  0x00b5292bU)
HPU_DEFINE_DLOAD(hpu_dload_poly_p0, 0x00b5102bU)
HPU_DEFINE_DLOAD(hpu_dload_poly_p1, 0x00b5122bU)
HPU_DEFINE_DLOAD(hpu_dload_poly_p2, 0x00b5142bU)
HPU_DEFINE_DLOAD(hpu_dload_poly_p3, 0x00b5162bU)
HPU_DEFINE_DLOAD(hpu_dload_poly_p4, 0x00b5182bU)
HPU_DEFINE_DLOAD(hpu_dload_poly_p5, 0x00b51a2bU)
HPU_DEFINE_DLOAD(hpu_dload_poly_p6, 0x00b51c2bU)
HPU_DEFINE_DLOAD(hpu_dload_poly_p7, 0x00b51e2bU)

HPU_DEFINE_DSTORE(hpu_dstore_poly_p0_release, 0x00b5502bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p1_release, 0x00b5522bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p2_release, 0x00b5542bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p3_release, 0x00b5562bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p4_release, 0x00b5582bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p5_release, 0x00b55a2bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p6_release, 0x00b55c2bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p7_release, 0x00b55e2bU)

/* Keep-object stores used by lifecycle and repeated-store tests. */
HPU_DEFINE_DSTORE(hpu_dstore_poly_p0_keep, 0x00b5402bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p2_keep, 0x00b5442bU)
HPU_DEFINE_DSTORE(hpu_dstore_poly_p7_keep, 0x00b54e2bU)

/*
 * Deliberately unchecked custom1 entry points for the CSR fault tests.
 *
 * The ordinary runtime wrappers reject zero-length and out-of-window spans
 * before issuing an instruction.  These aliases therefore exist separately
 * and must only be called by a target-side negative test after a legal window
 * has been committed.  Keeping the exact instruction words visible here also
 * lets the Nexus artifact check prove that the negative path was compiled.
 */
HPU_DEFINE_DLOAD(hpu_raw_custom1_dload_p0, 0x00b5102bU)
HPU_DEFINE_DLOAD(hpu_raw_custom1_dload_p7, 0x00b51e2bU)
HPU_DEFINE_DSTORE(hpu_raw_custom1_dstore_p7_keep, 0x00b54e2bU)

static inline void hpu_padd_p2_p0_p1(void) { HPU_WORD(0x0400400bU); }
static inline void hpu_padd_p2_p2_p0(void) { HPU_WORD(0x0480000bU); }
static inline void hpu_padd_p2_p2_p1(void) { HPU_WORD(0x0480400bU); }
static inline void hpu_psub_p2_p0_p1(void) { HPU_WORD(0x1400400bU); }
static inline void hpu_pmul_p2_p0_p1(void) { HPU_WORD(0x2400400bU); }
static inline void hpu_pmul_p0_p0_p1(void) { HPU_WORD(0x2000400bU); }
static inline void hpu_pmul_p2_p2_p0(void) { HPU_WORD(0x2480000bU); }
static inline void hpu_pmul_p2_p2_p1(void) { HPU_WORD(0x2480400bU); }
static inline void hpu_pmul_imm7_p2_p0(void) { HPU_WORD(0x2401c10bU); }
static inline void hpu_pmac_p2_p0_p1(void) { HPU_WORD(0x3400400bU); }
static inline void hpu_pmac_imm0_p2_p0(void) { HPU_WORD(0x3400010bU); }
static inline void hpu_pmac_imm1_p2_p0(void) { HPU_WORD(0x3400410bU); }
static inline void hpu_pmac_imm255_p2_p0(void) { HPU_WORD(0x343fc10bU); }
static inline void hpu_pmodld_0(void) { HPU_WORD(0x6000000bU); }
static inline void hpu_pmodld_1(void) { HPU_WORD(0x6000400bU); }
static inline void hpu_pmodld_2(void) { HPU_WORD(0x6000800bU); }
static inline void hpu_pmodld_3(void) { HPU_WORD(0x6000c00bU); }
static inline void hpu_pmodld_4(void) { HPU_WORD(0x6001000bU); }
static inline void hpu_pmodld_5(void) { HPU_WORD(0x6001400bU); }
static inline void hpu_pmodld_6(void) { HPU_WORD(0x6001800bU); }
static inline void hpu_psync(void) { HPU_WORD(0x7000000bU); }

static inline void hpu_pntt_p0_p1_stage(unsigned stage) {
    switch (stage) {
    case 0: HPU_WORD(0x4040000bU); break;
    case 1: HPU_WORD(0x4040040bU); break;
    case 2: HPU_WORD(0x4040080bU); break;
    case 3: HPU_WORD(0x40400c0bU); break;
    case 4: HPU_WORD(0x4040100bU); break;
    case 5: HPU_WORD(0x4040140bU); break;
    case 6: HPU_WORD(0x4040180bU); break;
    case 7: HPU_WORD(0x40401c0bU); break;
    case 8: HPU_WORD(0x4040200bU); break;
    case 9: HPU_WORD(0x4040240bU); break;
    case 10: HPU_WORD(0x4040280bU); break;
    case 11: HPU_WORD(0x40402c0bU); break;
    default: break;
    }
}

static inline void hpu_pintt_p0_p1_stage(unsigned stage) {
    switch (stage) {
    case 0: HPU_WORD(0x5040000bU); break;
    case 1: HPU_WORD(0x5040040bU); break;
    case 2: HPU_WORD(0x5040080bU); break;
    case 3: HPU_WORD(0x50400c0bU); break;
    case 4: HPU_WORD(0x5040100bU); break;
    case 5: HPU_WORD(0x5040140bU); break;
    case 6: HPU_WORD(0x5040180bU); break;
    case 7: HPU_WORD(0x50401c0bU); break;
    case 8: HPU_WORD(0x5040200bU); break;
    case 9: HPU_WORD(0x5040240bU); break;
    case 10: HPU_WORD(0x5040280bU); break;
    case 11: HPU_WORD(0x50402c0bU); break;
    default: break;
    }
}

static inline void hpu_pntt_p0_p1_stage0(void) { hpu_pntt_p0_p1_stage(0); }
static inline void hpu_pintt_p0_p1_stage0(void) { hpu_pintt_p0_p1_stage(0); }

static inline void hpu_pfree(unsigned obj) {
    switch (obj) {
    case 0: HPU_WORD(0x8000000bU); break;
    case 1: HPU_WORD(0x8040000bU); break;
    case 2: HPU_WORD(0x8080000bU); break;
    case 3: HPU_WORD(0x80c0000bU); break;
    case 4: HPU_WORD(0x8100000bU); break;
    case 5: HPU_WORD(0x8140000bU); break;
    case 6: HPU_WORD(0x8180000bU); break;
    case 7: HPU_WORD(0x81c0000bU); break;
    default: break;
    }
}

static inline void hpu_pfree_p0(void) { hpu_pfree(0); }
static inline void hpu_pfree_p1(void) { hpu_pfree(1); }
static inline void hpu_pfree_p2(void) { hpu_pfree(2); }
static inline void hpu_pfree_p3(void) { hpu_pfree(3); }
static inline void hpu_pfree_p4(void) { hpu_pfree(4); }
static inline void hpu_pfree_p5(void) { hpu_pfree(5); }
static inline void hpu_pfree_p6(void) { hpu_pfree(6); }
static inline void hpu_pfree_p7(void) { hpu_pfree(7); }

#endif
