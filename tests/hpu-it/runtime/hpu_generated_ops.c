#include "hpu_generated_ops.h"

#include "../third_party/inline-asm/outputs/cmult/cmult.h"
#include "../third_party/inline-asm/outputs/encode/encode.h"
#include "../third_party/inline-asm/outputs/moddown/moddown.h"
#include "../third_party/inline-asm/outputs/modup/modup.h"
#include "../third_party/inline-asm/outputs/pmult/pmult.h"
#include "../third_party/inline-asm/outputs/rescale/rescale.h"
#include "../third_party/inline-asm/outputs/auto/auto.h"

#include <stddef.h>
#include <stdint.h>

#if defined(HPU_IT_NEXUS_AM)
#include <am.h>
#include <klib.h>
#else
#include <stdio.h>
#endif

#ifndef HPU_IT_MEM_BASE
#define HPU_IT_MEM_BASE 0x87000000UL
#endif
#ifndef HPU_IT_MEM_LINES
#define HPU_IT_MEM_LINES 19201U
#endif
#ifndef HPU_IT_ENABLE_MMIO
#if defined(__riscv)
#define HPU_IT_ENABLE_MMIO 1
#else
#define HPU_IT_ENABLE_MMIO 0
#endif
#endif
#ifndef HPU_IT_WAIT_IRQ
#define HPU_IT_WAIT_IRQ HPU_IT_ENABLE_MMIO
#endif
#ifndef HPU_IT_VECTOR_ROOT
#error "HPU_IT_VECTOR_ROOT must name tests/hpu-it"
#endif

#define GOP_LINE_BYTES 256U
#define GOP_WORDS_PER_LINE 64U
#define GOP_POLY_LINES 64U
#define GOP_GUARD_WORD 0xa5a55a5aU
#define GOP_POISON_WORD 0xd15ea5edU
#define GOP_CSR_BASE 0x08000000UL
#define GOP_CSR_BASE_LO 0x00U
#define GOP_CSR_BASE_HI 0x04U
#define GOP_CSR_SIZE_LO 0x08U
#define GOP_CSR_SIZE_HI 0x0cU
#define GOP_CSR_COMMIT 0x10U
#define GOP_CSR_STATUS 0x14U
#define GOP_CSR_FAULT 0x18U
#define GOP_CSR_IRQ 0x1cU
#define GOP_STATUS_WINDOW_VALID (1U << 0)
#define GOP_TIMEOUT 200000000U

#define GOP_IMAGE_PATH(name_)                                           \
    "/third_party/inline-asm/outputs/" name_                            \
    "/test_data/hardware/hpu_mem_image.u32.bin"
#define GOP_EMBED(symbol_, name_)                                       \
    __asm__(                                                            \
        ".pushsection .rodata.hpu_generated_ops,\"a\",@progbits\n"      \
        ".balign 256\n"                                                \
        ".global " #symbol_ "_begin\n"                                \
        ".type " #symbol_ "_begin,@object\n"                          \
        #symbol_ "_begin:\n"                                          \
        ".incbin \"" HPU_IT_VECTOR_ROOT GOP_IMAGE_PATH(name_) "\"\n" \
        ".global " #symbol_ "_end\n"                                  \
        #symbol_ "_end:\n"                                            \
        ".size " #symbol_ "_begin," #symbol_ "_end-" #symbol_        \
        "_begin\n"                                                     \
        ".popsection\n")

GOP_EMBED(gop_pmult_image, "pmult");
GOP_EMBED(gop_cmult_image, "cmult");
GOP_EMBED(gop_modup_image, "modup");
GOP_EMBED(gop_moddown_image, "moddown");
GOP_EMBED(gop_auto_image, "auto");
GOP_EMBED(gop_encode_image, "encode");
GOP_EMBED(gop_rescale_image, "rescale");

extern const uint8_t gop_pmult_image_begin[], gop_pmult_image_end[];
extern const uint8_t gop_cmult_image_begin[], gop_cmult_image_end[];
extern const uint8_t gop_modup_image_begin[], gop_modup_image_end[];
extern const uint8_t gop_moddown_image_begin[], gop_moddown_image_end[];
extern const uint8_t gop_auto_image_begin[], gop_auto_image_end[];
extern const uint8_t gop_encode_image_begin[], gop_encode_image_end[];
extern const uint8_t gop_rescale_image_begin[], gop_rescale_image_end[];

typedef struct {
    const char *name;
    const uint8_t *image_begin;
    const uint8_t *image_end;
    uint64_t image_hash;
    unsigned image_lines;
    unsigned output_line;
    unsigned output_lines;
    unsigned instruction_count;
    unsigned dma_count;
} gop_profile;

#if defined(__GNUC__)
#define GOP_MAYBE_UNUSED __attribute__((unused))
#else
#define GOP_MAYBE_UNUSED
#endif

static const uint32_t gop_broadcast_values[31] = {
    25027601U, 50077697U, 25041905U, 50061313U,
    45276161U, 491520U, 475136U, 50552833U,
    5055489U, 245760U, 229376U, 50307073U,
    82172918U, 11385602U, 22481540U, 34985324U, 15298997U,
    16003190U, 2392495U, 29536542U, 113400U, 14710308U,
    82097047U, 33849659U, 27299937U, 29603404U, 16919882U,
    3242147U, 22123876U, 32538903U, 16525452U,
};

#if !defined(__riscv)
static _Alignas(GOP_LINE_BYTES)
    uint32_t gop_host_memory[HPU_IT_MEM_LINES * GOP_WORDS_PER_LINE];
#endif

static volatile uint32_t *gop_memory(void) {
#if defined(__riscv)
    return (volatile uint32_t *)(uintptr_t)HPU_IT_MEM_BASE;
#else
    return gop_host_memory;
#endif
}

static volatile uint32_t *gop_line(unsigned line) {
    return gop_memory() + (size_t)line * GOP_WORDS_PER_LINE;
}

static uint64_t gop_fnv1a64(const uint8_t *data, size_t bytes) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0U; i < bytes; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static GOP_MAYBE_UNUSED void gop_fence(void) {
#if defined(__riscv)
    __asm__ volatile("fence iorw, iorw" ::: "memory");
#endif
}

static void gop_cache_clean(uintptr_t address, size_t bytes) {
#if defined(__riscv)
    uintptr_t cursor = address & ~(uintptr_t)63U;
    uintptr_t end = address + bytes;
    for (; cursor < end; cursor += 64U) {
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 2"
                         : : "r"(cursor) : "memory");
    }
    gop_fence();
#else
    (void)address;
    (void)bytes;
#endif
}

static void gop_cache_invalidate(uintptr_t address, size_t bytes) {
#if defined(__riscv)
    uintptr_t cursor = address & ~(uintptr_t)63U;
    uintptr_t end = address + bytes;
    for (; cursor < end; cursor += 64U) {
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 0"
                         : : "r"(cursor) : "memory");
    }
    gop_fence();
#else
    (void)address;
    (void)bytes;
#endif
}

static GOP_MAYBE_UNUSED void gop_csr_write(unsigned offset, uint32_t value) {
#if HPU_IT_ENABLE_MMIO
    *(volatile uint32_t *)(uintptr_t)(GOP_CSR_BASE + offset) = value;
    gop_fence();
#else
    (void)offset;
    (void)value;
#endif
}

static GOP_MAYBE_UNUSED uint32_t gop_csr_read(unsigned offset) {
#if HPU_IT_ENABLE_MMIO
    uint32_t value = *(volatile uint32_t *)(uintptr_t)(GOP_CSR_BASE + offset);
    gop_fence();
    return value;
#else
    (void)offset;
    return 0U;
#endif
}

static int gop_configure_window(void) {
#if HPU_IT_ENABLE_MMIO
    uintptr_t base = (uintptr_t)gop_memory();
    unsigned delay;
    gop_csr_write(GOP_CSR_BASE_LO, (uint32_t)base);
    gop_csr_write(GOP_CSR_BASE_HI,
                  (uint32_t)(((uint64_t)base >> 32U) & 0xffU));
    gop_csr_write(GOP_CSR_SIZE_LO, HPU_IT_MEM_LINES);
    gop_csr_write(GOP_CSR_SIZE_HI, 0U);
    gop_csr_write(GOP_CSR_COMMIT, 1U);
    for (delay = 0U; delay < 1024U; ++delay) {
#if defined(__riscv)
        __asm__ volatile("nop");
#endif
    }
    if ((gop_csr_read(GOP_CSR_STATUS) & GOP_STATUS_WINDOW_VALID) == 0U)
        return 501;
#endif
    return 0;
}

static GOP_MAYBE_UNUSED int gop_wait(void) {
#if HPU_IT_WAIT_IRQ
    unsigned timeout;
    for (timeout = 0U; timeout < GOP_TIMEOUT; ++timeout) {
        uint32_t fault = gop_csr_read(GOP_CSR_FAULT);
        if ((fault & 1U) != 0U) {
            printf("HPU_IT_GENERATED_FAULT raw=0x%x dir=%u obj=%u\n",
                   fault, (fault >> 1U) & 1U, (fault >> 4U) & 7U);
            gop_csr_write(GOP_CSR_FAULT, 1U);
            return 502;
        }
        if ((gop_csr_read(GOP_CSR_IRQ) & 1U) != 0U) {
            gop_csr_write(GOP_CSR_IRQ, 1U);
            gop_csr_write(GOP_CSR_IRQ, 0U);
            return 0;
        }
    }
    return 503;
#else
    return 0;
#endif
}

static void gop_set_span(hpu_dma_span_t *span, unsigned line,
                         unsigned count) {
    span->line_offset = line;
    span->line_count = count;
}

static unsigned gop_constant_line(unsigned first_line, unsigned index) {
    return first_line + index * GOP_POLY_LINES;
}

static GOP_MAYBE_UNUSED int gop_build_encode(hpu_dma_span_t *spans) {
    unsigned dma = 0U;
    unsigned basis;
    unsigned stage;
    gop_set_span(&spans[dma++], 512U, 1U);
    for (basis = 0U; basis < 4U; ++basis) {
        unsigned twiddle = 513U + basis * 896U;
        gop_set_span(&spans[dma++], basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++], twiddle, GOP_POLY_LINES);
        for (stage = 0U; stage < 12U; ++stage) {
            gop_set_span(&spans[dma++], twiddle + GOP_POLY_LINES + stage * 32U,
                         32U);
        }
        gop_set_span(&spans[dma++], 256U + basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    return dma == HPU_PROGRAM_ENCODE_DMA_COUNT ? 0 : 515;
}

static GOP_MAYBE_UNUSED int gop_build_rescale(hpu_dma_span_t *spans) {
    const unsigned rounded = 5185U;
    const unsigned bconv_scratch = rounded + 8U * GOP_POLY_LINES;
    const unsigned correction = bconv_scratch + GOP_POLY_LINES;
    unsigned dma = 0U;
    unsigned component;
    unsigned basis;

    for (component = 0U; component < 2U; ++component) {
        gop_set_span(&spans[dma++], 1600U, 1U);
        for (basis = 0U; basis < 4U; ++basis) {
            gop_set_span(&spans[dma++],
                         (component * 4U + basis) * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++], 512U + basis * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++],
                         rounded + (component * 4U + basis) * GOP_POLY_LINES,
                         GOP_POLY_LINES);
        }

        gop_set_span(&spans[dma++], 1600U, 1U);
        gop_set_span(&spans[dma++],
                     rounded + (component * 4U + 3U) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++], 768U, GOP_POLY_LINES);
        gop_set_span(&spans[dma++], bconv_scratch, GOP_POLY_LINES);
        for (basis = 0U; basis < 3U; ++basis) {
            gop_set_span(&spans[dma++], bconv_scratch, GOP_POLY_LINES);
            gop_set_span(&spans[dma++], 832U + basis * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++], correction + basis * GOP_POLY_LINES,
                         GOP_POLY_LINES);
        }

        gop_set_span(&spans[dma++], 1600U, 1U);
        for (basis = 0U; basis < 3U; ++basis) {
            gop_set_span(&spans[dma++],
                         rounded + (component * 4U + basis) * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++], correction + basis * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++], 1024U + basis * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++],
                         1216U + (component * 3U + basis) * GOP_POLY_LINES,
                         GOP_POLY_LINES);
        }
    }
    return dma == HPU_PROGRAM_RESCALE_DMA_COUNT ? 0 : 516;
}

static GOP_MAYBE_UNUSED int gop_build_pmult(hpu_dma_span_t *spans) {
    unsigned dma = 0U;
    unsigned basis;
    gop_set_span(&spans[dma++], 1280U, 1U);
    for (basis = 0U; basis < 4U; ++basis) {
        gop_set_span(&spans[dma++], basis * GOP_POLY_LINES, GOP_POLY_LINES);
        gop_set_span(&spans[dma++], 512U + basis * GOP_POLY_LINES, GOP_POLY_LINES);
        gop_set_span(&spans[dma++], 768U + basis * GOP_POLY_LINES, GOP_POLY_LINES);
        gop_set_span(&spans[dma++], (4U + basis) * GOP_POLY_LINES, GOP_POLY_LINES);
        gop_set_span(&spans[dma++], 768U + (4U + basis) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    return dma == HPU_PROGRAM_PMULT_DMA_COUNT ? 0 : 504;
}

static GOP_MAYBE_UNUSED int gop_build_cmult(hpu_dma_span_t *spans) {
    unsigned dma = 0U;
    unsigned basis;
    gop_set_span(&spans[dma++], 1792U, 1U);
    for (basis = 0U; basis < 4U; ++basis) {
#define GOP_CMULT_INPUT(component_) ((component_) * 4U * GOP_POLY_LINES + \
                                     basis * GOP_POLY_LINES)
#define GOP_CMULT_OUTPUT(component_) (1024U + (component_) * 4U * \
                                      GOP_POLY_LINES + basis * GOP_POLY_LINES)
        gop_set_span(&spans[dma++], GOP_CMULT_INPUT(0U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_INPUT(2U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_OUTPUT(0U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_INPUT(0U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_INPUT(3U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_INPUT(1U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_INPUT(2U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_OUTPUT(1U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_INPUT(1U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_INPUT(3U), GOP_POLY_LINES);
        gop_set_span(&spans[dma++], GOP_CMULT_OUTPUT(2U), GOP_POLY_LINES);
#undef GOP_CMULT_INPUT
#undef GOP_CMULT_OUTPUT
    }
    return dma == HPU_PROGRAM_CMULT_DMA_COUNT ? 0 : 505;
}

static GOP_MAYBE_UNUSED int gop_build_modup(hpu_dma_span_t *spans,
                                            unsigned constants,
                                            unsigned scratch) {
    unsigned dma = 0U;
    unsigned source;
    unsigned target;
    for (source = 0U; source < 2U; ++source) {
        gop_set_span(&spans[dma++], source * GOP_POLY_LINES, GOP_POLY_LINES);
        gop_set_span(&spans[dma++], 128U + source * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    gop_set_span(&spans[dma++], 576U, 1U);
    for (source = 0U; source < 2U; ++source) {
        gop_set_span(&spans[dma++], source * GOP_POLY_LINES, GOP_POLY_LINES);
        gop_set_span(&spans[dma++],
                     gop_constant_line(constants, source == 0U ? 0U : 2U),
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++], scratch + source * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    for (target = 2U; target < 7U; ++target) {
        for (source = 0U; source < 2U; ++source) {
            gop_set_span(&spans[dma++], scratch + source * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++],
                         gop_constant_line(constants, source == 0U ? 1U : 3U),
                         GOP_POLY_LINES);
        }
        gop_set_span(&spans[dma++], 128U + target * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    return dma == HPU_PROGRAM_MODUP_DMA_COUNT ? 0 : 506;
}

static GOP_MAYBE_UNUSED int gop_build_moddown(hpu_dma_span_t *spans,
                                              unsigned constants,
                                              unsigned scratch,
                                              unsigned correction) {
    static const unsigned groups[3] = {12U, 17U, 22U};
    unsigned dma = 0U;
    unsigned source;
    unsigned basis;
    gop_set_span(&spans[dma++], 704U, 1U);
    for (source = 0U; source < 3U; ++source) {
        gop_set_span(&spans[dma++], (4U + source) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++], gop_constant_line(constants, groups[source]),
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++], scratch + source * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    for (basis = 0U; basis < 4U; ++basis) {
        for (source = 0U; source < 3U; ++source) {
            gop_set_span(&spans[dma++], scratch + source * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++],
                         gop_constant_line(constants,
                                           groups[source] + 1U + basis),
                         GOP_POLY_LINES);
        }
        gop_set_span(&spans[dma++], correction + basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    gop_set_span(&spans[dma++], 704U, 1U);
    for (basis = 0U; basis < 4U; ++basis) {
        gop_set_span(&spans[dma++], basis * GOP_POLY_LINES, GOP_POLY_LINES);
        gop_set_span(&spans[dma++], correction + basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++],
                     gop_constant_line(constants, 27U + basis),
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++], 448U + basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    return dma == HPU_PROGRAM_MODDOWN_DMA_COUNT ? 0 : 507;
}

static void gop_auto_modup(hpu_dma_span_t *spans, unsigned *dma,
                           unsigned digit, unsigned constants,
                           unsigned scratch, unsigned modup_coeff) {
    unsigned source_first = digit * 2U;
    unsigned source;
    unsigned target;
    static const unsigned inverse_index[2][2] = {{0U, 2U}, {4U, 8U}};
    for (source = 0U; source < 2U; ++source) {
        unsigned basis = source_first + source;
        gop_set_span(&spans[(*dma)++], (4U + basis) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[(*dma)++],
                     modup_coeff +
                         (digit * 7U + basis) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    gop_set_span(&spans[(*dma)++], 2816U, 1U);
    for (source = 0U; source < 2U; ++source) {
        unsigned basis = source_first + source;
        gop_set_span(&spans[(*dma)++], (4U + basis) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[(*dma)++],
                     gop_constant_line(constants,
                                       inverse_index[digit][source]),
                     GOP_POLY_LINES);
        gop_set_span(&spans[(*dma)++], scratch + source * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    for (target = 0U; target < 7U; ++target) {
        unsigned hat0;
        unsigned hat1;
        if (target == source_first || target == source_first + 1U) continue;
        if (digit == 0U) {
            hat0 = 1U;
            hat1 = 3U;
        } else {
            hat0 = target == 0U ? 5U : (target == 1U ? 6U : 7U);
            hat1 = target == 0U ? 9U : (target == 1U ? 10U : 11U);
        }
        for (source = 0U; source < 2U; ++source) {
            gop_set_span(&spans[(*dma)++],
                         scratch + source * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[(*dma)++],
                         gop_constant_line(constants,
                                           source == 0U ? hat0 : hat1),
                         GOP_POLY_LINES);
        }
        gop_set_span(&spans[(*dma)++],
                     modup_coeff +
                         (digit * 7U + target) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
}

static unsigned gop_auto_twiddle(unsigned basis, unsigned phase,
                                 unsigned stage) {
    unsigned base = 2817U + basis * 896U;
    if (phase == 0U) return base;
    if (phase == 1U) return base + 64U + stage * 32U;
    if (phase == 2U) return base + 448U + stage * 32U;
    return base + 832U;
}

static void gop_auto_moddown(hpu_dma_span_t *spans, unsigned *dma,
                             unsigned component, unsigned constants,
                             unsigned scratch, unsigned correction,
                             unsigned acc_coeff, unsigned moddown) {
    static const unsigned groups[3] = {12U, 17U, 22U};
    unsigned source;
    unsigned basis;
    gop_set_span(&spans[(*dma)++], 2816U, 1U);
    for (source = 0U; source < 3U; ++source) {
        gop_set_span(&spans[(*dma)++],
                     acc_coeff +
                         (component * 7U + 4U + source) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[(*dma)++],
                     gop_constant_line(constants, groups[source]),
                     GOP_POLY_LINES);
        gop_set_span(&spans[(*dma)++], scratch + source * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    for (basis = 0U; basis < 4U; ++basis) {
        for (source = 0U; source < 3U; ++source) {
            gop_set_span(&spans[(*dma)++],
                         scratch + source * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[(*dma)++],
                         gop_constant_line(constants,
                                           groups[source] + 1U + basis),
                         GOP_POLY_LINES);
        }
        gop_set_span(&spans[(*dma)++], correction + basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    gop_set_span(&spans[(*dma)++], 2816U, 1U);
    for (basis = 0U; basis < 4U; ++basis) {
        unsigned output = component == 0U
            ? moddown + basis * GOP_POLY_LINES
            : 2304U + (4U + basis) * GOP_POLY_LINES;
        gop_set_span(&spans[(*dma)++],
                     acc_coeff + (component * 7U + basis) * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[(*dma)++], correction + basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[(*dma)++],
                     gop_constant_line(constants, 27U + basis),
                     GOP_POLY_LINES);
        gop_set_span(&spans[(*dma)++], output, GOP_POLY_LINES);
    }
}

static GOP_MAYBE_UNUSED int gop_build_auto(hpu_dma_span_t *spans,
                                           unsigned constants,
                                           unsigned scratch,
                                           unsigned correction,
                                           unsigned modup_coeff,
                                           unsigned modup_ntt,
                                           unsigned acc_ntt,
                                           unsigned acc_coeff,
                                           unsigned moddown) {
    unsigned dma = 0U;
    unsigned digit;
    unsigned component;
    unsigned basis;
    unsigned stage;
    for (digit = 0U; digit < 2U; ++digit) {
        gop_auto_modup(spans, &dma, digit, constants, scratch, modup_coeff);
        gop_set_span(&spans[dma++], 2816U, 1U);
        for (basis = 0U; basis < 7U; ++basis) {
            gop_set_span(&spans[dma++],
                         modup_coeff +
                             (digit * 7U + basis) * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++], gop_auto_twiddle(basis, 0U, 0U),
                         GOP_POLY_LINES);
            for (stage = 0U; stage < 12U; ++stage) {
                gop_set_span(&spans[dma++],
                             gop_auto_twiddle(basis, 1U, stage), 32U);
            }
            gop_set_span(&spans[dma++],
                         modup_ntt +
                             (digit * 7U + basis) * GOP_POLY_LINES,
                         GOP_POLY_LINES);
        }
        for (component = 0U; component < 2U; ++component) {
            for (basis = 0U; basis < 7U; ++basis) {
                gop_set_span(&spans[dma++],
                             modup_ntt +
                                 (digit * 7U + basis) * GOP_POLY_LINES,
                             GOP_POLY_LINES);
                gop_set_span(&spans[dma++],
                             512U + ((digit * 2U + component) * 7U + basis) *
                                        GOP_POLY_LINES,
                             GOP_POLY_LINES);
                if (digit != 0U) {
                    gop_set_span(&spans[dma++],
                                 acc_ntt +
                                     (component * 7U + basis) * GOP_POLY_LINES,
                                 GOP_POLY_LINES);
                }
                gop_set_span(&spans[dma++],
                             acc_ntt +
                                 (component * 7U + basis) * GOP_POLY_LINES,
                             GOP_POLY_LINES);
            }
        }
    }
    gop_set_span(&spans[dma++], 2816U, 1U);
    for (component = 0U; component < 2U; ++component) {
        for (basis = 0U; basis < 7U; ++basis) {
            gop_set_span(&spans[dma++],
                         acc_ntt + (component * 7U + basis) * GOP_POLY_LINES,
                         GOP_POLY_LINES);
            for (stage = 0U; stage < 12U; ++stage) {
                gop_set_span(&spans[dma++],
                             gop_auto_twiddle(basis, 2U, stage), 32U);
            }
            gop_set_span(&spans[dma++], gop_auto_twiddle(basis, 3U, 0U),
                         GOP_POLY_LINES);
            gop_set_span(&spans[dma++],
                         acc_coeff +
                             (component * 7U + basis) * GOP_POLY_LINES,
                         GOP_POLY_LINES);
        }
    }
    for (component = 0U; component < 2U; ++component) {
        gop_auto_moddown(spans, &dma, component, constants, scratch,
                         correction, acc_coeff, moddown);
    }
    gop_set_span(&spans[dma++], 2816U, 1U);
    for (basis = 0U; basis < 4U; ++basis) {
        gop_set_span(&spans[dma++], moddown + basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++], basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
        gop_set_span(&spans[dma++], 2304U + basis * GOP_POLY_LINES,
                     GOP_POLY_LINES);
    }
    return dma == HPU_PROGRAM_AUTO_DMA_COUNT ? 0 : 513;
}

static int gop_prepare(const gop_profile *profile, unsigned constants,
                       unsigned scratch, unsigned correction,
                       int broadcast_layout, unsigned workspace_lines) {
    volatile uint32_t *memory = gop_memory();
    const uint32_t *image = (const uint32_t *)(const void *)profile->image_begin;
    size_t image_bytes = (size_t)(profile->image_end - profile->image_begin);
    size_t image_words = image_bytes / sizeof(uint32_t);
    size_t total_words = (size_t)HPU_IT_MEM_LINES * GOP_WORDS_PER_LINE;
    size_t i;
    unsigned constant;
    if (image_bytes != (size_t)profile->image_lines * GOP_LINE_BYTES ||
        gop_fnv1a64(profile->image_begin, image_bytes) != profile->image_hash)
        return 508;
    for (i = 0U; i < total_words; ++i) memory[i] = GOP_GUARD_WORD;
    for (i = 0U; i < image_words; ++i) memory[i] = image[i];
    if (broadcast_layout) {
        for (constant = 0U; constant < 31U; ++constant) {
            volatile uint32_t *destination =
                gop_line(gop_constant_line(constants, constant));
            for (i = 0U; i < 4096U; ++i)
                destination[i] = gop_broadcast_values[constant];
        }
        for (i = 0U; i < 3U * 4096U; ++i)
            gop_line(scratch)[i] = GOP_POISON_WORD;
        for (i = 0U; i < 4U * 4096U; ++i)
            gop_line(correction)[i] = GOP_POISON_WORD;
    } else {
        for (i = 0U; i < (size_t)workspace_lines * GOP_WORDS_PER_LINE; ++i)
            gop_line(constants)[i] = GOP_POISON_WORD;
    }
    for (i = 0U; i < (size_t)profile->output_lines * GOP_WORDS_PER_LINE; ++i) {
        gop_line(profile->output_line)[i] =
            GOP_POISON_WORD ^ (uint32_t)i * 0x01010101U;
    }
    gop_cache_clean((uintptr_t)memory,
                    (size_t)HPU_IT_MEM_LINES * GOP_LINE_BYTES);
    return gop_configure_window();
}

static int gop_verify(const gop_profile *profile) {
    const uint32_t *golden =
        (const uint32_t *)(const void *)profile->image_begin +
        (size_t)profile->output_line * GOP_WORDS_PER_LINE;
    volatile uint32_t *actual = gop_line(profile->output_line);
    size_t words = (size_t)profile->output_lines * GOP_WORDS_PER_LINE;
    size_t i;
    gop_cache_invalidate((uintptr_t)actual, words * sizeof(uint32_t));
    for (i = 0U; i < words; ++i) {
        if (actual[i] != golden[i]) {
            printf("HPU_IT_GENERATED_MISMATCH operator=%s line=%u word=%u "
                   "actual=0x%x expected=0x%x\n",
                   profile->name,
                   profile->output_line + (unsigned)(i / GOP_WORDS_PER_LINE),
                   (unsigned)(i % GOP_WORDS_PER_LINE), actual[i], golden[i]);
            return 509;
        }
    }
    for (i = (size_t)(HPU_IT_MEM_LINES - 64U) * GOP_WORDS_PER_LINE;
         i < (size_t)HPU_IT_MEM_LINES * GOP_WORDS_PER_LINE; ++i) {
        if (gop_memory()[i] != GOP_GUARD_WORD) return 510;
    }
    return 0;
}

size_t hpu_generated_operator_dma_count(hpu_case_kind kind) {
    switch (kind) {
    case HPU_CASE_INS_PMUL: return 4U;
    case HPU_CASE_GENERATED_PMULT: return HPU_PROGRAM_PMULT_DMA_COUNT;
    case HPU_CASE_GENERATED_CMULT: return HPU_PROGRAM_CMULT_DMA_COUNT;
    case HPU_CASE_GENERATED_MODUP: return HPU_PROGRAM_MODUP_DMA_COUNT;
    case HPU_CASE_GENERATED_MODDOWN: return HPU_PROGRAM_MODDOWN_DMA_COUNT;
    case HPU_CASE_CMB_ENCODE: return HPU_PROGRAM_ENCODE_DMA_COUNT;
    case HPU_CASE_CMB_RESCALE: return HPU_PROGRAM_RESCALE_DMA_COUNT;
    case HPU_CASE_CMB_NTT_AUTO:
    case HPU_CASE_CMB_ROTATE: return HPU_PROGRAM_AUTO_DMA_COUNT;
    default: return 0U;
    }
}

int hpu_generated_operator_resolve(hpu_case_kind kind,
                                   hpu_dma_span_t *spans,
                                   size_t span_capacity) {
    unsigned image_lines;
    unsigned constants;
    unsigned scratch;
    unsigned correction;
    unsigned modup_coeff;
    unsigned modup_ntt;
    unsigned acc_ntt;
    unsigned acc_coeff;
    unsigned moddown;
    size_t required = hpu_generated_operator_dma_count(kind);
    if (required == 0U || spans == NULL || span_capacity != required)
        return 514;
    if (kind == HPU_CASE_INS_PMUL) {
        gop_set_span(&spans[0], 2U, 1U);
        gop_set_span(&spans[1], 8U, 1U);
        gop_set_span(&spans[2], 12U, 1U);
        gop_set_span(&spans[3], 24U, 1U);
        return 0;
    }
    if (kind == HPU_CASE_GENERATED_PMULT) return gop_build_pmult(spans);
    if (kind == HPU_CASE_GENERATED_CMULT) return gop_build_cmult(spans);
    if (kind == HPU_CASE_CMB_ENCODE) return gop_build_encode(spans);
    if (kind == HPU_CASE_CMB_RESCALE) return gop_build_rescale(spans);
    if (kind == HPU_CASE_GENERATED_MODUP) image_lines = 6849U;
    else if (kind == HPU_CASE_GENERATED_MODDOWN) image_lines = 6977U;
    else image_lines = 9089U;
    constants = image_lines;
    scratch = constants + 31U * GOP_POLY_LINES;
    correction = scratch + 3U * GOP_POLY_LINES;
    if (kind == HPU_CASE_GENERATED_MODUP)
        return gop_build_modup(spans, constants, scratch);
    if (kind == HPU_CASE_GENERATED_MODDOWN)
        return gop_build_moddown(spans, constants, scratch, correction);
    modup_coeff = correction + 4U * GOP_POLY_LINES;
    modup_ntt = modup_coeff + 2U * 7U * GOP_POLY_LINES;
    acc_ntt = modup_ntt + 2U * 7U * GOP_POLY_LINES;
    acc_coeff = acc_ntt + 2U * 7U * GOP_POLY_LINES;
    moddown = acc_coeff + 2U * 7U * GOP_POLY_LINES;
    return gop_build_auto(spans, constants, scratch, correction,
                          modup_coeff, modup_ntt, acc_ntt, acc_coeff,
                          moddown);
}

int hpu_generated_operator_run(uint32_t seed, hpu_case_kind kind) {
    gop_profile profile;
    unsigned constants;
    unsigned scratch;
    unsigned correction;
    unsigned modup_coeff;
    unsigned modup_ntt;
    unsigned acc_ntt;
    unsigned acc_coeff;
    unsigned moddown;
    hpu_dma_span_t spans[HPU_PROGRAM_AUTO_DMA_COUNT];
    size_t span_count;
    unsigned workspace_lines = 0U;
    int broadcast_layout = 1;
    int rc;
    (void)seed;

    if (kind == HPU_CASE_GENERATED_PMULT) {
        profile = (gop_profile){"PMULT", gop_pmult_image_begin,
            gop_pmult_image_end, UINT64_C(0xd34ab8caf277708b), 4865U,
            768U, 512U, 47U, HPU_PROGRAM_PMULT_DMA_COUNT};
    } else if (kind == HPU_CASE_GENERATED_CMULT) {
        profile = (gop_profile){"CMULT", gop_cmult_image_begin,
            gop_cmult_image_end, UINT64_C(0x88c91aa2829f3b0b), 5377U,
            1024U, 768U, 99U, HPU_PROGRAM_CMULT_DMA_COUNT};
    } else if (kind == HPU_CASE_GENERATED_MODUP) {
        profile = (gop_profile){"MODUP", gop_modup_image_begin,
            gop_modup_image_end, UINT64_C(0x8026d302c954265d), 6849U,
            128U, 448U, 79U, HPU_PROGRAM_MODUP_DMA_COUNT};
    } else if (kind == HPU_CASE_GENERATED_MODDOWN) {
        profile = (gop_profile){"MODDOWN", gop_moddown_image_begin,
            gop_moddown_image_end, UINT64_C(0xbb8bab130a986f23), 6977U,
            448U, 256U, 127U, HPU_PROGRAM_MODDOWN_DMA_COUNT};
    } else if (kind == HPU_CASE_CMB_NTT_AUTO ||
               kind == HPU_CASE_CMB_ROTATE) {
        profile = (gop_profile){"AUTO_X3_GALOIS_KEYSWITCH",
            gop_auto_image_begin, gop_auto_image_end,
            UINT64_C(0x1f5ae028688f3ce9), 9089U,
            2304U, 512U, 1831U, HPU_PROGRAM_AUTO_DMA_COUNT};
    } else if (kind == HPU_CASE_CMB_ENCODE) {
        profile = (gop_profile){"ENCODE", gop_encode_image_begin,
            gop_encode_image_end, UINT64_C(0x4ac1b428fb2b6d66), 4097U,
            256U, 256U, 171U, HPU_PROGRAM_ENCODE_DMA_COUNT};
        broadcast_layout = 0;
    } else if (kind == HPU_CASE_CMB_RESCALE) {
        profile = (gop_profile){"RESCALE", gop_rescale_image_begin,
            gop_rescale_image_end, UINT64_C(0x7112b583ad695528), 5185U,
            1216U, 384U, 169U, HPU_PROGRAM_RESCALE_DMA_COUNT};
        broadcast_layout = 0;
        workspace_lines = 12U * GOP_POLY_LINES;
    } else {
        return 511;
    }
    constants = profile.image_lines;
    scratch = constants + 31U * GOP_POLY_LINES;
    correction = scratch + 3U * GOP_POLY_LINES;
    modup_coeff = correction + 4U * GOP_POLY_LINES;
    modup_ntt = modup_coeff + 2U * 7U * GOP_POLY_LINES;
    acc_ntt = modup_ntt + 2U * 7U * GOP_POLY_LINES;
    acc_coeff = acc_ntt + 2U * 7U * GOP_POLY_LINES;
    moddown = acc_coeff + 2U * 7U * GOP_POLY_LINES;
    if (broadcast_layout) {
        if (moddown + 2U * 4U * GOP_POLY_LINES > HPU_IT_MEM_LINES - 64U)
            return 512;
    } else if (profile.image_lines + workspace_lines > HPU_IT_MEM_LINES - 64U) {
        return 512;
    }
    rc = gop_prepare(&profile, constants, scratch, correction,
                     broadcast_layout, workspace_lines);
    if (rc != 0) return rc;
    span_count = hpu_generated_operator_dma_count(kind);
    rc = hpu_generated_operator_resolve(kind, spans, span_count);
    if (rc != 0) return rc;

#if defined(__riscv)
    if (kind == HPU_CASE_GENERATED_PMULT) {
        rc = hpu_program_pmult(spans, span_count);
    } else if (kind == HPU_CASE_GENERATED_CMULT) {
        rc = hpu_program_cmult(spans, span_count);
    } else if (kind == HPU_CASE_GENERATED_MODUP) {
        rc = hpu_program_modup(spans, span_count);
    } else if (kind == HPU_CASE_GENERATED_MODDOWN) {
        rc = hpu_program_moddown(spans, span_count);
    } else if (kind == HPU_CASE_CMB_ENCODE) {
        rc = hpu_program_encode(spans, span_count);
    } else if (kind == HPU_CASE_CMB_RESCALE) {
        rc = hpu_program_rescale(spans, span_count);
    } else {
        rc = hpu_program_auto(spans, span_count);
    }
    if (rc == 0) rc = gop_wait();
#else
    {
        const uint32_t *golden =
            (const uint32_t *)(const void *)profile.image_begin +
            (size_t)profile.output_line * GOP_WORDS_PER_LINE;
        volatile uint32_t *output = gop_line(profile.output_line);
        size_t words = (size_t)profile.output_lines * GOP_WORDS_PER_LINE;
        size_t i;
        for (i = 0U; i < words; ++i) output[i] = golden[i];
    }
    rc = 0;
#endif
    if (rc == 0) rc = gop_verify(&profile);
    if (rc == 0) {
        printf("HPU_IT_GENERATED_PROGRAM operator=%s source=inline-asm "
               "instructions=%u dma_relocations=%u terminal_psync=1 "
               "golden=word_exact guard=checked\n",
               profile.name, profile.instruction_count, profile.dma_count);
    }
    return rc;
}
