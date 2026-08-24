#include <hpu/arithmetic.h>
#include <hpu/cache.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/init.h>
#include <hpu/layout.h>
#include <hpu/result.h>
#include <hpu/sync.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_07_DLOAD_COMPUTE_DSTORE_PSYNC";
    volatile uint32_t *mod = hpu_line(HPU_LINE_MOD);
    uint64_t mu = UINT64_MAX / HPU_MODULUS;
    unsigned i;
    int rc;

    hpu_test_begin(case_id,
                   "dload+padd+dstore+terminal-psync+self-check");

    if (hpu_fixture_validate_embedded() != 0) {
        return hpu_test_fail(case_id, "fixture", 1U);
    }
    rc = hpu_initialize_and_verify();
    if (rc != 0) return hpu_test_fail(case_id, "hpu-init", (unsigned)rc);

    /* Prepare one modulus record; fixture helpers prepare A, B, and output. */
    for (i = 0U; i < HPU_WORDS_PER_LINE; ++i) {
        mod[i] = 0U;
    }
    mod[0] = HPU_MODULUS;
    mod[1] = (uint32_t)mu;
    mod[2] = (uint32_t)(mu >> 32U);
    mod[3] = 0U;
    hpu_fence();
    hpu_cache_clean((uintptr_t)mod, HPU_LINE_BYTES);
    hpu_fixture_copy_inputs_to_ddr();
    hpu_fixture_poison_output();

    /* 4096-coefficient program: load modulus/A/B, PADD, store, synchronize. */
    hpu_dload_mod_p4(HPU_LINE_MOD, 1U);
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);
    hpu_dload_p1(HPU_LINE_SRC_B, HPU_RNS_LINES);
    hpu_pmodld_0();
    hpu_padd_p2_p0_p1();
    hpu_dstore_p2_release(HPU_LINE_OUTPUT, HPU_RNS_LINES);
    hpu_psync();

    rc = hpu_wait_completion_and_clear();
    if (rc != 0) {
        return hpu_test_fail(case_id, "padd-psync", (unsigned)rc);
    }
    if (hpu_fixture_check_padd() != 0) {
        return hpu_test_fail(case_id, "coefficient-compare", 1U);
    }
    printf("HPU_SMOKE_COMPARE_PASS operation=padd coefficients=%u\n",
           HPU_RNS_COEFFICIENTS);
    return hpu_test_end(case_id, 0);
}
