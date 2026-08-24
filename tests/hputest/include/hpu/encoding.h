#ifndef HPU_ENCODING_H
#define HPU_ENCODING_H

#include <stdint.h>

/*
 * HPU instruction words produced by the authoritative inline-asm encoder:
 *
 *   repository: https://github.com/cuihu2/inline-asm
 *   commit:     4399883b9e1fa249b99d48c7e919ee52acc662bc
 *   encoder:    encode/src/encoder.cpp
 *
 * GNU as does not know the HPU mnemonics, so the C adapters emit these words
 * with `.word`.  GitHub Actions recompiles the pinned encoder and checks every
 * mnemonic/word pair below; do not edit a value without updating the encoder.
 */
#define HPU_INLINE_ASM_SOURCE_COMMIT "4399883b9e1fa249b99d48c7e919ee52acc662bc"

/* dload x10, x11, p0, poly, regular-bank */
#define HPU_INSN_DLOAD_P0_POLY UINT32_C(0x00b5102b)
/* dload x10, x11, p1, poly, regular-bank */
#define HPU_INSN_DLOAD_P1_POLY UINT32_C(0x00b5122b)
/* dload x10, x11, p4, mod-ctx, small-bank */
#define HPU_INSN_DLOAD_P4_MOD UINT32_C(0x00b5292b)
/* dstore x10, x11, p0, release */
#define HPU_INSN_DSTORE_P0_RELEASE UINT32_C(0x00b5502b)
/* dstore x10, x11, p2, release */
#define HPU_INSN_DSTORE_P2_RELEASE UINT32_C(0x00b5542b)

/* pmodld 0 */
#define HPU_INSN_PMODLD_0 UINT32_C(0x6000000b)
/* padd p2, p0, p1 */
#define HPU_INSN_PADD_P2_P0_P1 UINT32_C(0x0400400b)
/* psync */
#define HPU_INSN_PSYNC UINT32_C(0x7000000b)

#endif
