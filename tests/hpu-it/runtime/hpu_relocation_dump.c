#include "hpu_generated_ops.h"
#include "hpu_fhe.h"
#include "../third_party/inline-asm/outputs/ciphertext_multiply/ciphertext_multiply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static hpu_case_kind parse_kind(const char *value) {
    if (strcmp(value, "mm") == 0) return HPU_CASE_INS_PMUL;
    if (strcmp(value, "pmult") == 0) return HPU_CASE_GENERATED_PMULT;
    if (strcmp(value, "cmult") == 0) return HPU_CASE_GENERATED_CMULT;
    if (strcmp(value, "modup") == 0) return HPU_CASE_GENERATED_MODUP;
    if (strcmp(value, "moddown") == 0) return HPU_CASE_GENERATED_MODDOWN;
    if (strcmp(value, "auto") == 0) return HPU_CASE_CMB_ROTATE;
    if (strcmp(value, "encode") == 0) return HPU_CASE_CMB_ENCODE;
    if (strcmp(value, "rescale") == 0) return HPU_CASE_CMB_RESCALE;
    if (strcmp(value, "keyswitch") == 0) return HPU_CASE_CMB_KEYSWITCH;
    if (strcmp(value, "relinearization") == 0) return HPU_CASE_CMB_RELINE;
    if (strcmp(value, "ciphertext_multiply") == 0) return HPU_CASE_CMB_HMUL;
    return (hpu_case_kind)-1;
}

static int is_fhe_kind(hpu_case_kind kind) {
    return kind == HPU_CASE_CMB_KEYSWITCH ||
           kind == HPU_CASE_CMB_RELINE ||
           kind == HPU_CASE_CMB_HMUL;
}

static const char *logical_ref(hpu_case_kind kind, unsigned line) {
    static char text[96];
    unsigned index;
    if (kind == HPU_CASE_CMB_ENCODE) {
        if (line < 256U) {
            snprintf(text, sizeof(text), "encode_input_coeff[q%u]", line / 64U);
            return text;
        }
        if (line < 512U) {
            snprintf(text, sizeof(text), "encode_output_ntt[q%u]",
                     (line - 256U) / 64U);
            return text;
        }
        if (line == 512U) return "mod_context[Q4]";
        if (line >= 513U && line < 4097U) {
            unsigned basis = (line - 513U) / 896U;
            unsigned local = (line - 513U) % 896U;
            if (local == 0U) {
                snprintf(text, sizeof(text), "ntt_pre_twist[q%u]", basis);
            } else {
                snprintf(text, sizeof(text), "ntt_twiddle[q%u][stage%u]",
                         basis, (local - 64U) / 32U);
            }
            return text;
        }
    } else if (kind == HPU_CASE_CMB_RESCALE) {
        if (line < 512U) {
            index = line / 64U;
            snprintf(text, sizeof(text), "rescale_input[c%u][q%u]",
                     index / 4U, index % 4U);
            return text;
        }
        if (line < 768U) {
            snprintf(text, sizeof(text), "q_last_half_mod_q[q%u]",
                     (line - 512U) / 64U);
            return text;
        }
        if (line == 768U) return "qhat_inv_drop[q3]";
        if (line >= 832U && line < 1024U) {
            snprintf(text, sizeof(text), "qhat_mod_qprime[q%u]",
                     (line - 832U) / 64U);
            return text;
        }
        if (line >= 1024U && line < 1216U) {
            snprintf(text, sizeof(text), "q_last_inv_mod_qprime[q%u]",
                     (line - 1024U) / 64U);
            return text;
        }
        if (line >= 1216U && line < 1600U) {
            index = (line - 1216U) / 64U;
            snprintf(text, sizeof(text), "rescale_output[c%u][q%u]",
                     index / 3U, index % 3U);
            return text;
        }
        if (line == 1600U) return "mod_context[Q4]";
        if (line >= 5185U && line < 5697U) {
            index = (line - 5185U) / 64U;
            snprintf(text, sizeof(text), "rounded_numerator[c%u][q%u]",
                     index / 4U, index % 4U);
            return text;
        }
        if (line == 5697U) return "bconv_scratch[q3]";
        if (line >= 5761U && line < 5953U) {
            snprintf(text, sizeof(text), "rescale_correction[q%u]",
                     (line - 5761U) / 64U);
            return text;
        }
    } else if (kind == HPU_CASE_INS_PMUL) {
        if (line == 2U) return "mod_context[q0]";
        if (line == 8U) return "input_a";
        if (line == 12U) return "input_b";
        if (line == 24U) return "output";
    } else if (kind == HPU_CASE_GENERATED_PMULT) {
        if (line == 1280U) return "mod_context[Q4]";
        if (line < 512U) {
            snprintf(text, sizeof(text), "ciphertext[c%u][q%u]",
                     line / 256U, (line / 64U) % 4U);
            return text;
        }
        if (line < 768U) {
            snprintf(text, sizeof(text), "plaintext[q%u]", (line - 512U) / 64U);
            return text;
        }
        if (line < 1280U) {
            snprintf(text, sizeof(text), "output[c%u][q%u]",
                     (line - 768U) / 256U, ((line - 768U) / 64U) % 4U);
            return text;
        }
    } else if (kind == HPU_CASE_GENERATED_CMULT) {
        if (line == 1792U) return "mod_context[Q4]";
        if (line < 1024U) {
            static const char *names[4] = {"a0", "a1", "b0", "b1"};
            snprintf(text, sizeof(text), "input[%s][q%u]",
                     names[line / 256U], (line / 64U) % 4U);
            return text;
        }
        if (line < 1792U) {
            snprintf(text, sizeof(text), "tensor[t%u][q%u]",
                     (line - 1024U) / 256U, ((line - 1024U) / 64U) % 4U);
            return text;
        }
    } else if (kind == HPU_CASE_GENERATED_MODUP) {
        if (line < 128U) {
            snprintf(text, sizeof(text), "input_digit[q%u]", line / 64U);
            return text;
        }
        if (line < 576U) {
            snprintf(text, sizeof(text), "output_qp[basis%u]", (line - 128U) / 64U);
            return text;
        }
        if (line == 576U) return "mod_context[QP7]";
        if (line >= 6849U && line < 8833U) {
            snprintf(text, sizeof(text), "broadcast_constant[%u]", (line - 6849U) / 64U);
            return text;
        }
        if (line >= 8833U && line < 9025U) {
            snprintf(text, sizeof(text), "bconv_scratch[source%u]", (line - 8833U) / 64U);
            return text;
        }
    } else if (kind == HPU_CASE_GENERATED_MODDOWN) {
        if (line < 448U) {
            snprintf(text, sizeof(text), "input_qp[basis%u]", line / 64U);
            return text;
        }
        if (line < 704U) {
            snprintf(text, sizeof(text), "output_q[q%u]", (line - 448U) / 64U);
            return text;
        }
        if (line == 704U) return "mod_context[QP7]";
        if (line >= 6977U && line < 8961U) {
            snprintf(text, sizeof(text), "broadcast_constant[%u]", (line - 6977U) / 64U);
            return text;
        }
        if (line >= 8961U && line < 9153U) {
            snprintf(text, sizeof(text), "bconv_scratch[p%u]", (line - 8961U) / 64U);
            return text;
        }
        if (line >= 9153U && line < 9409U) {
            snprintf(text, sizeof(text), "correction_q[q%u]", (line - 9153U) / 64U);
            return text;
        }
    } else if (is_fhe_kind(kind)) {
        if (line < 512U) {
            snprintf(text, sizeof(text), "ct_a[c%u][q%u]",
                     line / 256U, (line / 64U) % 4U);
            return text;
        }
        if (line < 1024U) {
            snprintf(text, sizeof(text), "ct_b[c%u][q%u]",
                     (line - 512U) / 256U, ((line - 512U) / 64U) % 4U);
            return text;
        }
        if (line >= 1408U && line < 3200U) {
            index = (line - 1408U) / 64U;
            snprintf(text, sizeof(text), "relinearization_key[d%u][c%u][basis%u]",
                     index / 14U, (index / 7U) % 2U, index % 7U);
            return text;
        }
        if (line >= 3200U && line < 4224U) {
            index = (line - 3200U) / 64U;
            snprintf(text, sizeof(text), "input_ntt[input%u][q%u]",
                     index / 4U, index % 4U);
            return text;
        }
        if (line >= 4224U && line < 4992U) {
            index = (line - 4224U) / 64U;
            snprintf(text, sizeof(text), "tensor_ntt[t%u][q%u]",
                     index / 4U, index % 4U);
            return text;
        }
        if (line >= 4992U && line < 5760U) {
            index = (line - 4992U) / 64U;
            snprintf(text, sizeof(text), "tensor_coeff[t%u][q%u]",
                     index / 4U, index % 4U);
            return text;
        }
        if (line >= 5760U && line < 6656U) {
            index = (line - 5760U) / 64U;
            snprintf(text, sizeof(text), "modup_coeff[d%u][basis%u]",
                     index / 7U, index % 7U);
            return text;
        }
        if (line >= 6656U && line < 7552U) {
            index = (line - 6656U) / 64U;
            snprintf(text, sizeof(text), "modup_ntt[d%u][basis%u]",
                     index / 7U, index % 7U);
            return text;
        }
        if (line >= 7552U && line < 8448U) {
            index = (line - 7552U) / 64U;
            snprintf(text, sizeof(text), "keyswitch_acc_ntt[c%u][basis%u]",
                     index / 7U, index % 7U);
            return text;
        }
        if (line >= 8448U && line < 9344U) {
            index = (line - 8448U) / 64U;
            snprintf(text, sizeof(text), "keyswitch_coeff[c%u][basis%u]",
                     index / 7U, index % 7U);
            return text;
        }
        if (line >= 9344U && line < 9856U) {
            index = (line - 9344U) / 64U;
            snprintf(text, sizeof(text), "keyswitch_moddown[c%u][q%u]",
                     index / 4U, index % 4U);
            return text;
        }
        if (line >= 9856U && line < 10368U) {
            index = (line - 9856U) / 64U;
            snprintf(text, sizeof(text), "ciphertext_output[c%u][q%u]",
                     index / 4U, index % 4U);
            return text;
        }
        if (line == 10688U) return "mod_context[QP7]";
        if (line >= 10689U && line < 16961U) {
            index = line - 10689U;
            snprintf(text, sizeof(text), "twiddle[basis%u][line%u]",
                     index / 896U, index % 896U);
            return text;
        }
        if (line >= 16961U && line < 18945U) {
            snprintf(text, sizeof(text), "broadcast_constant[%u]",
                     (line - 16961U) / 64U);
            return text;
        }
        if (line >= 18945U && line < 19137U) {
            snprintf(text, sizeof(text), "bconv_scratch[source%u]",
                     (line - 18945U) / 64U);
            return text;
        }
    } else {
        if (line < 512U) {
            snprintf(text, sizeof(text), "rotated_input[c%u][q%u]",
                     line / 256U, (line / 64U) % 4U);
            return text;
        }
        if (line < 2304U) {
            index = (line - 512U) / 64U;
            snprintf(text, sizeof(text), "galois_key[d%u][c%u][basis%u]",
                     index / 14U, (index / 7U) % 2U, index % 7U);
            return text;
        }
        if (line < 2816U) {
            index = (line - 2304U) / 64U;
            snprintf(text, sizeof(text), "auto_output[c%u][q%u]", index / 4U, index % 4U);
            return text;
        }
        if (line == 2816U) return "mod_context[QP7]";
        if (line >= 2817U && line < 9089U) {
            index = line - 2817U;
            snprintf(text, sizeof(text), "twiddle[basis%u][line%u]", index / 896U, index % 896U);
            return text;
        }
        if (line >= 9089U && line < 11073U) {
            snprintf(text, sizeof(text), "broadcast_constant[%u]", (line - 9089U) / 64U);
            return text;
        }
        if (line >= 11073U && line < 11265U) {
            snprintf(text, sizeof(text), "bconv_scratch[%u]", (line - 11073U) / 64U);
            return text;
        }
        if (line >= 11265U && line < 11521U) {
            snprintf(text, sizeof(text), "correction_q[q%u]", (line - 11265U) / 64U);
            return text;
        }
        if (line >= 11521U && line < 12417U) {
            index = (line - 11521U) / 64U;
            snprintf(text, sizeof(text), "modup_coeff[d%u][basis%u]", index / 7U, index % 7U);
            return text;
        }
        if (line >= 12417U && line < 13313U) {
            index = (line - 12417U) / 64U;
            snprintf(text, sizeof(text), "modup_ntt[d%u][basis%u]", index / 7U, index % 7U);
            return text;
        }
        if (line >= 13313U && line < 14209U) {
            index = (line - 13313U) / 64U;
            snprintf(text, sizeof(text), "keyswitch_acc_ntt[c%u][basis%u]", index / 7U, index % 7U);
            return text;
        }
        if (line >= 14209U && line < 15105U) {
            index = (line - 14209U) / 64U;
            snprintf(text, sizeof(text), "keyswitch_acc_coeff[c%u][basis%u]", index / 7U, index % 7U);
            return text;
        }
        if (line >= 15105U && line < 15617U) {
            index = (line - 15105U) / 64U;
            snprintf(text, sizeof(text), "moddown[c%u][q%u]", index / 4U, index % 4U);
            return text;
        }
    }
    snprintf(text, sizeof(text), "unclassified[line%u]", line);
    return text;
}

int main(int argc, char **argv) {
    hpu_dma_span_t spans[HPU_PROGRAM_CIPHERTEXT_MULTIPLY_DMA_COUNT];
    hpu_case_kind kind;
    size_t count;
    size_t row = 0U;
    FILE *input;
    FILE *output;
    char line[1024];
    if (argc != 4) return 2;
    kind = parse_kind(argv[1]);
    if (is_fhe_kind(kind)) {
        if (hpu_fhe_resolve_generated_program(
                kind, spans, HPU_PROGRAM_CIPHERTEXT_MULTIPLY_DMA_COUNT,
                &count) != 0)
            return 3;
    } else {
        count = hpu_generated_operator_dma_count(kind);
        if (count == 0U ||
            hpu_generated_operator_resolve(kind, spans, count) != 0)
            return 3;
    }
    input = fopen(argv[2], "r");
    output = fopen(argv[3], "w");
    if (input == NULL || output == NULL) return 4;
    if (fgets(line, sizeof(line), input) == NULL) return 5;
    line[strcspn(line, "\r\n")] = '\0';
    fprintf(output, "%s,logical_data_ref,line_offset,line_count,end_line_exclusive,relocation_status\n", line);
    while (fgets(line, sizeof(line), input) != NULL) {
        unsigned dma_index;
        char *first;
        char *second;
        if (row >= count) return 6;
        line[strcspn(line, "\r\n")] = '\0';
        first = strchr(line, ',');
        second = first == NULL ? NULL : strchr(first + 1, ',');
        if (first == NULL || second == NULL) return 7;
        dma_index = (unsigned)strtoul(first + 1, NULL, 10);
        if (dma_index != row || spans[row].line_count == 0U ||
            spans[row].line_offset >= 19201U ||
            spans[row].line_count > 19201U - spans[row].line_offset)
            return 8;
        fprintf(output, "%s,\"%s\",%u,%u,%u,RESOLVED\n", line,
                logical_ref(kind, spans[row].line_offset),
                spans[row].line_offset, spans[row].line_count,
                spans[row].line_offset + spans[row].line_count);
        ++row;
    }
    if (row != count || fclose(input) != 0 || fclose(output) != 0) return 9;
    return 0;
}
