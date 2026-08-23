#include "hpu_vectors.h"

#ifndef HPU_IT_VECTOR_ROOT
#error "HPU_IT_VECTOR_ROOT must name the IT-SCPU-TestCases source directory"
#endif

/*
 * Build compact Q0..Q3 execution images from the upstream ciphertext package.
 * Each image contains one four-limb polynomial, an independent golden slot,
 * the Q/P mod-context line, and the four Q-basis transform constants.  The
 * runtime poisons the golden slot before execution and relocates line offsets
 * to the configured shared-DRAM window.
 */
#define HPU_CT_HW                                                        \
    "/third_party/inline-asm/outputs/ciphertext_multiply/test_data/hardware"
#define HPU_INCBIN(path_)                                                \
    ".incbin \"" HPU_IT_VECTOR_ROOT HPU_CT_HW path_ "\"\n"
#define HPU_INCBIN_PART(path_, skip_, count_)                            \
    ".incbin \"" HPU_IT_VECTOR_ROOT HPU_CT_HW path_ "\"," #skip_ "," \
        #count_ "\n"
#define HPU_STAGE_FILES(direction_, basis_)                              \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_00.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_01.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_02.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_03.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_04.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_05.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_06.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_07.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_08.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_09.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_10.u32.bin")                                    \
    HPU_INCBIN("/constants/twiddle/" direction_ "/basis_" basis_        \
               "/stage_11.u32.bin")
#define HPU_NTT_BASIS(basis_)                                           \
    HPU_INCBIN("/constants/twiddle/ntt/basis_" basis_                   \
               "/pre_twist.u32.bin")                                  \
    HPU_STAGE_FILES("ntt", basis_)
#define HPU_INTT_BASIS(basis_)                                          \
    HPU_STAGE_FILES("intt", basis_)                                    \
    HPU_INCBIN("/constants/twiddle/intt/basis_" basis_                  \
               "/post_untwist_scale.u32.bin")
#define HPU_EMBED_VECTOR(symbol_, body_)                                 \
    __asm__(                                                              \
        ".pushsection .rodata.hpu_vectors,\"a\",@progbits\n"            \
        ".balign 256\n"                                                  \
        ".global " #symbol_ "_begin\n"                                  \
        ".type " #symbol_ "_begin,@object\n"                            \
        #symbol_ "_begin:\n"                                            \
        body_                                                             \
        ".global " #symbol_ "_end\n"                                    \
        #symbol_ "_end:\n"                                              \
        ".size " #symbol_ "_begin," #symbol_ "_end-" #symbol_          \
            "_begin\n"                                                   \
        ".popsection\n")

HPU_EMBED_VECTOR(
    hpu_vector_ntt,
    HPU_INCBIN_PART("/images/input/ct_a_q.u32.bin", 0, 65536)
    HPU_INCBIN_PART("/images/expected/inputs_ntt_q.u32.bin", 0, 65536)
    HPU_INCBIN("/constants/mod_ctx.u32.bin")
    HPU_NTT_BASIS("00")
    HPU_NTT_BASIS("01")
    HPU_NTT_BASIS("02")
    HPU_NTT_BASIS("03"));

HPU_EMBED_VECTOR(
    hpu_vector_intt,
    HPU_INCBIN_PART("/images/expected/inputs_ntt_q.u32.bin", 0, 65536)
    HPU_INCBIN_PART("/images/input/ct_a_q.u32.bin", 0, 65536)
    HPU_INCBIN("/constants/mod_ctx.u32.bin")
    HPU_INTT_BASIS("00")
    HPU_INTT_BASIS("01")
    HPU_INTT_BASIS("02")
    HPU_INTT_BASIS("03"));

extern const uint8_t hpu_vector_ntt_begin[];
extern const uint8_t hpu_vector_ntt_end[];
extern const uint8_t hpu_vector_intt_begin[];
extern const uint8_t hpu_vector_intt_end[];

/* Offsets are frozen by the matching upstream hardware/line_map.csv files. */
static const hpu_vector_image hpu_vectors[] = {
    {
        hpu_vector_ntt_begin,
        hpu_vector_ntt_end,
        UINT64_C(0x1379fa73a0804152),
        2305U,
        0U,
        256U,
        512U,
        513U,
        577U,
        577U,
        513U,
        256U,
        64U,
        32U,
        12U,
        4U,
        448U,
    },
    {
        hpu_vector_intt_begin,
        hpu_vector_intt_end,
        UINT64_C(0xeabcc424d6a4367d),
        2305U,
        0U,
        256U,
        512U,
        897U,
        513U,
        513U,
        897U,
        256U,
        64U,
        32U,
        12U,
        4U,
        448U,
    },
};

const hpu_vector_image *hpu_get_vector_image(hpu_vector_kind kind) {
    if (kind != HPU_VECTOR_NTT && kind != HPU_VECTOR_INTT) return NULL;
    return &hpu_vectors[(unsigned)kind];
}

size_t hpu_vector_image_size(const hpu_vector_image *image) {
    if (image == NULL || image->image_begin == NULL ||
        image->image_end < image->image_begin) {
        return 0U;
    }
    return (size_t)(image->image_end - image->image_begin);
}
