#ifndef HPU_ENCODING_H
#define HPU_ENCODING_H

/*
 * This tracked header intentionally contains no copied instruction words.
 * `make prepare-inline-asm-mm` runs the pinned producer's real encoder and
 * creates this build-only header before any testcase is compiled.
 */
#include <hpu/inline_asm_mm_delivery.h>

#endif
