#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/init.h>
#include <hpu/result.h>
#include <hpu/sync.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_06_DLOAD_DSTORE_PSYNC";
    int rc;

    hpu_test_begin(case_id, "dload+dstore+terminal-psync+self-check");

    if (hpu_fixture_validate_embedded() != 0) {
        return hpu_test_fail(case_id, "fixture", 1U);
    }
    rc = hpu_initialize_and_verify();
    if (rc != 0) return hpu_test_fail(case_id, "hpu-init", (unsigned)rc);

    hpu_fixture_copy_inputs_to_ddr();
    hpu_fixture_poison_output();
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);
    hpu_dstore_p0_release(HPU_LINE_OUTPUT, HPU_RNS_LINES);
    hpu_psync();

    rc = hpu_wait_completion_and_clear();
    if (rc != 0) {
        return hpu_test_fail(case_id, "dload-dstore-psync", (unsigned)rc);
    }
    if (hpu_fixture_check_dload_dstore() != 0) {
        return hpu_test_fail(case_id, "coefficient-compare", 1U);
    }
    printf("HPU_SMOKE_COMPARE_PASS operation=dload-dstore coefficients=%u\n",
           HPU_RNS_COEFFICIENTS);
    return hpu_test_end(case_id, 0);
}
