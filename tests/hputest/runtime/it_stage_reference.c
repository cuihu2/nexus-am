#include <hpu/stage_vectors.h>
#include <stddef.h>

/* loader 寄存器 index 对应的对象物理字地址，不是数学系数编号。 */
static unsigned physical_index(unsigned base, unsigned half, unsigned index) {
    if (half < 128U) return base + index;
    return base + (index >> 1U) + (index & 1U) * half;
}

int stage_golden(const uint32_t *input, const uint32_t *twiddle,
                 uint32_t *output, unsigned words, uint32_t q,
                 unsigned stage, unsigned inverse) {
    unsigned log_n = 0U;
    if (input == NULL || twiddle == NULL || output == NULL || input == output ||
        words < 128U || words > 65536U || (words & (words - 1U)) != 0U ||
        q < 65537U || inverse > 1U)
        return 1;
    for (unsigned value = words; value > 1U; value >>= 1U) ++log_n;
    if (stage >= log_n) return 1;

    /* PINTT 的 stage k 使用正向 log2(N)-1-k 的 loader。 */
    const unsigned forward_stage = inverse ? log_n - 1U - stage : stage;
    const unsigned half = 1U << forward_stage;
    const unsigned group_words = half < 128U ? 128U : 2U * half;
    const unsigned offsets = half < 128U ? 1U : half / 64U;
    unsigned cursor = 0U;
    for (unsigned group = 0U; group < words; group += group_words) {
        for (unsigned offset = 0U; offset < offsets; ++offset) {
            const unsigned base = group + offset * 64U;
            for (unsigned lane = 0U; lane < 64U; ++lane) {
                /* 正向：相邻蝶形后 P；逆向：先 P^-1 再相邻蝶形。 */
                const unsigned read_a = inverse ? lane : 2U * lane;
                const unsigned read_b = inverse ? lane + 64U : 2U * lane + 1U;
                const unsigned write_a = inverse ? 2U * lane : lane;
                const unsigned write_b = inverse ? 2U * lane + 1U : lane + 64U;
                const uint32_t a = input[physical_index(base, half, read_a)];
                const uint32_t source_b = input[physical_index(base, half, read_b)];
                const uint32_t w = twiddle[cursor++];
                if (a >= q || source_b >= q || w >= q) return 1;
                const uint32_t b = (uint32_t)((uint64_t)source_b * w % q);
                const uint64_t sum = (uint64_t)a + b;
                output[physical_index(base, half, write_a)] = (uint32_t)(sum >= q ? sum - q : sum);
                output[physical_index(base, half, write_b)] = a >= b ? a - b : q - (b - a);
            }
        }
    }
    return cursor != words / 2U;
}
