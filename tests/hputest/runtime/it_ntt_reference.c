#include <hpu/ntt_reference.h>

#include <stddef.h>

static uint32_t bit_reverse_12(uint32_t value) {
    uint32_t reversed = 0U;
    for (unsigned bit = 0U; bit < HPU_NTT_REFERENCE_STAGES; ++bit) {
        reversed = (reversed << 1U) | (value & 1U);
        value >>= 1U;
    }
    return reversed;
}

static uint32_t mod_multiply(uint32_t left, uint32_t right, uint32_t modulus) {
    return (uint32_t)(((uint64_t)left * right) % modulus);
}

static uint32_t mod_power(uint32_t base, uint32_t exponent, uint32_t modulus) {
    uint32_t result = 1U;
    while (exponent != 0U) {
        if (exponent & 1U) result = mod_multiply(result, base, modulus);
        base = mod_multiply(base, base, modulus);
        exponent >>= 1U;
    }
    return result;
}

int hpu_ntt_reference_prepare(struct hpu_ntt_reference *reference,
                              const uint32_t *physical_input,
                              const uint32_t *physical_twist,
                              uint32_t modulus) {
    if (reference == NULL || physical_input == NULL || physical_twist == NULL ||
        modulus < 3U)
        return 1;
    reference->modulus = modulus;
    reference->post_scale = 1U;
    reference->twist_step = 1U;
    for (unsigned physical = 0U; physical < HPU_NTT_REFERENCE_N; ++physical) {
        const unsigned logical = bit_reverse_12(physical);
        if (physical_input[physical] >= modulus || physical_twist[physical] >= modulus)
            return 1;
        reference->bit_reverse[physical] = (uint16_t)logical;
        reference->input[logical] = physical_input[physical];
        reference->twist[logical] = physical_twist[physical];
    }

    /* bit_reverse(2048)==1，因此该物理位置保存primitive 2N-th root psi。 */
    const uint32_t psi = physical_twist[HPU_NTT_REFERENCE_N / 2U];
    const uint32_t omega = mod_multiply(psi, psi, modulus);
    if (mod_power(psi, HPU_NTT_REFERENCE_N, modulus) != modulus - 1U ||
        mod_power(psi, 2U * HPU_NTT_REFERENCE_N, modulus) != 1U)
        return 1;
    for (unsigned stage = 0U; stage < HPU_NTT_REFERENCE_STAGES; ++stage) {
        const unsigned length = 1U << (stage + 1U);
        reference->stage_step[stage] =
            mod_power(omega, HPU_NTT_REFERENCE_N / length, modulus);
    }
    return 0;
}

static void forward_physical_layout(uint32_t *values) {
    uint32_t batch[128];
    uint32_t changed[128];

    for (unsigned stage = 0U; stage < HPU_NTT_REFERENCE_STAGES; ++stage) {
        const unsigned half = 1U << stage;
        if (half < 128U) {
            for (unsigned base = 0U; base < HPU_NTT_REFERENCE_N; base += 128U) {
                for (unsigned lane = 0U; lane < 128U; ++lane)
                    batch[lane] = values[base + lane];
                for (unsigned lane = 0U; lane < 64U; ++lane) {
                    changed[lane] = batch[2U * lane];
                    changed[64U + lane] = batch[2U * lane + 1U];
                }
                for (unsigned lane = 0U; lane < 128U; ++lane)
                    values[base + lane] = changed[lane];
            }
        } else {
            for (unsigned group = 0U; group < HPU_NTT_REFERENCE_N; group += 2U * half) {
                for (unsigned offset = 0U; offset < half; offset += 64U) {
                    for (unsigned lane = 0U; lane < 64U; ++lane) {
                        batch[2U * lane] = values[group + offset + lane];
                        batch[2U * lane + 1U] =
                            values[group + offset + lane + half];
                    }
                    for (unsigned lane = 0U; lane < 64U; ++lane) {
                        changed[lane] = batch[2U * lane];
                        changed[64U + lane] = batch[2U * lane + 1U];
                    }
                    for (unsigned index = 0U; index < 128U; ++index) {
                        const unsigned lane = index / 2U;
                        const unsigned side = index & 1U;
                        values[group + offset + lane + side * half] = changed[index];
                    }
                }
            }
        }
    }
}

static void inverse_physical_layout(uint32_t *values) {
    uint32_t batch[128];
    uint32_t changed[128];

    for (unsigned reverse = 0U; reverse < HPU_NTT_REFERENCE_STAGES; ++reverse) {
        const unsigned stage = HPU_NTT_REFERENCE_STAGES - 1U - reverse;
        const unsigned half = 1U << stage;
        if (half < 128U) {
            for (unsigned base = 0U; base < HPU_NTT_REFERENCE_N; base += 128U) {
                for (unsigned lane = 0U; lane < 128U; ++lane)
                    batch[lane] = values[base + lane];
                for (unsigned lane = 0U; lane < 64U; ++lane) {
                    changed[2U * lane] = batch[lane];
                    changed[2U * lane + 1U] = batch[64U + lane];
                }
                for (unsigned lane = 0U; lane < 128U; ++lane)
                    values[base + lane] = changed[lane];
            }
        } else {
            for (unsigned group = 0U; group < HPU_NTT_REFERENCE_N; group += 2U * half) {
                for (unsigned offset = 0U; offset < half; offset += 64U) {
                    for (unsigned lane = 0U; lane < 64U; ++lane) {
                        batch[2U * lane] = values[group + offset + lane];
                        batch[2U * lane + 1U] =
                            values[group + offset + lane + half];
                    }
                    for (unsigned lane = 0U; lane < 64U; ++lane) {
                        changed[2U * lane] = batch[lane];
                        changed[2U * lane + 1U] = batch[64U + lane];
                    }
                    for (unsigned lane = 0U; lane < 64U; ++lane) {
                        values[group + offset + lane] = changed[2U * lane];
                        values[group + offset + lane + half] = changed[2U * lane + 1U];
                    }
                }
            }
        }
    }
}

void hpu_ntt_reference_run(struct hpu_ntt_reference *reference) {
    const uint32_t modulus = reference->modulus;
    for (unsigned index = 0U; index < HPU_NTT_REFERENCE_N; ++index)
        reference->output[index] =
            mod_multiply(reference->input[index], reference->twist[index], modulus);

    for (unsigned index = 0U; index < HPU_NTT_REFERENCE_N; ++index) {
        const unsigned reversed = reference->bit_reverse[index];
        if (index < reversed) {
            const uint32_t saved = reference->output[index];
            reference->output[index] = reference->output[reversed];
            reference->output[reversed] = saved;
        }
    }

    for (unsigned stage = 0U; stage < HPU_NTT_REFERENCE_STAGES; ++stage) {
        const unsigned length = 1U << (stage + 1U);
        const unsigned half = length / 2U;
        for (unsigned base = 0U; base < HPU_NTT_REFERENCE_N; base += length) {
            uint32_t factor = 1U;
            for (unsigned lane = 0U; lane < half; ++lane) {
                const uint32_t left = reference->output[base + lane];
                const uint32_t right = mod_multiply(
                    reference->output[base + lane + half], factor, modulus);
                reference->output[base + lane] =
                    left >= modulus - right ? left - (modulus - right) : left + right;
                reference->output[base + lane + half] =
                    left >= right ? left - right : modulus - (right - left);
                factor = mod_multiply(factor, reference->stage_step[stage], modulus);
            }
        }
    }
    forward_physical_layout(reference->output);
}

int hpu_intt_reference_prepare(struct hpu_ntt_reference *reference,
                               const uint32_t *physical_input,
                               const uint32_t *physical_post_factor,
                               uint32_t modulus) {
    if (reference == NULL || physical_input == NULL || physical_post_factor == NULL ||
        modulus < 3U)
        return 1;
    reference->modulus = modulus;
    for (unsigned physical = 0U; physical < HPU_NTT_REFERENCE_N; ++physical) {
        if (physical_input[physical] >= modulus ||
            physical_post_factor[physical] >= modulus)
            return 1;
        reference->bit_reverse[physical] = (uint16_t)bit_reverse_12(physical);
        reference->input[physical] = physical_input[physical];
        reference->twist[physical] = physical_post_factor[physical];
    }
    inverse_physical_layout(reference->input);

    const uint32_t n_inverse = reference->twist[0];
    const uint32_t psi_inverse = mod_multiply(
        reference->twist[HPU_NTT_REFERENCE_N / 2U],
        mod_power(n_inverse, modulus - 2U, modulus), modulus);
    const uint32_t psi = mod_power(psi_inverse, modulus - 2U, modulus);
    if (mod_multiply(n_inverse, HPU_NTT_REFERENCE_N, modulus) != 1U ||
        mod_power(psi, HPU_NTT_REFERENCE_N, modulus) != modulus - 1U ||
        mod_power(psi, 2U * HPU_NTT_REFERENCE_N, modulus) != 1U)
        return 1;
    reference->post_scale = n_inverse;
    reference->twist_step = psi_inverse;
    for (unsigned physical = 0U; physical < HPU_NTT_REFERENCE_N; ++physical) {
        const uint32_t expected = mod_multiply(
            n_inverse,
            mod_power(psi_inverse, reference->bit_reverse[physical], modulus), modulus);
        if (reference->twist[physical] != expected) return 1;
    }

    const uint32_t omega_inverse = mod_multiply(psi_inverse, psi_inverse, modulus);
    for (unsigned stage = 0U; stage < HPU_NTT_REFERENCE_STAGES; ++stage) {
        const unsigned length = 1U << (stage + 1U);
        reference->stage_step[stage] =
            mod_power(omega_inverse, HPU_NTT_REFERENCE_N / length, modulus);
    }
    return 0;
}

void hpu_intt_reference_run(struct hpu_ntt_reference *reference) {
    const uint32_t modulus = reference->modulus;
    for (unsigned index = 0U; index < HPU_NTT_REFERENCE_N; ++index)
        reference->output[index] = reference->input[index];

    for (unsigned index = 0U; index < HPU_NTT_REFERENCE_N; ++index) {
        const unsigned reversed = reference->bit_reverse[index];
        if (index < reversed) {
            const uint32_t saved = reference->output[index];
            reference->output[index] = reference->output[reversed];
            reference->output[reversed] = saved;
        }
    }

    for (unsigned stage = 0U; stage < HPU_NTT_REFERENCE_STAGES; ++stage) {
        const unsigned length = 1U << (stage + 1U);
        const unsigned half = length / 2U;
        for (unsigned base = 0U; base < HPU_NTT_REFERENCE_N; base += length) {
            uint32_t factor = 1U;
            for (unsigned lane = 0U; lane < half; ++lane) {
                const uint32_t left = reference->output[base + lane];
                const uint32_t right = mod_multiply(
                    reference->output[base + lane + half], factor, modulus);
                reference->output[base + lane] =
                    left >= modulus - right ? left - (modulus - right) : left + right;
                reference->output[base + lane + half] =
                    left >= right ? left - right : modulus - (right - left);
                factor = mod_multiply(factor, reference->stage_step[stage], modulus);
            }
        }
    }

    uint32_t inverse_twist = 1U;
    for (unsigned index = 0U; index < HPU_NTT_REFERENCE_N; ++index) {
        reference->output[index] = mod_multiply(
            mod_multiply(reference->output[index], reference->post_scale, modulus),
            inverse_twist, modulus);
        inverse_twist = mod_multiply(inverse_twist, reference->twist_step, modulus);
    }

    /* coefficient-domain delivery uses memory[p] = logical[bit_reverse(p)]. */
    for (unsigned index = 0U; index < HPU_NTT_REFERENCE_N; ++index) {
        const unsigned reversed = reference->bit_reverse[index];
        if (index < reversed) {
            const uint32_t saved = reference->output[index];
            reference->output[index] = reference->output[reversed];
            reference->output[reversed] = saved;
        }
    }
}
