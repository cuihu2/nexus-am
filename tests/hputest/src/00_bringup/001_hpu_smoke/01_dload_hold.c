#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/init.h>
#include <hpu/result.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_01_DLOAD_HOLD";
    int rc;

    hpu_test_begin(case_id, "dload+while(1)");

    if (hpu_fixture_validate_embedded() != 0) {
        return hpu_test_fail(case_id, "fixture", 1U);
    }
    rc = hpu_initialize_and_verify();
    if (rc != 0) return hpu_test_fail(case_id, "hpu-init", (unsigned)rc);

    hpu_fixture_copy_inputs_to_ddr();
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);

    /* Intentional: leave enough simulation time to inspect DLOAD waves. */
    for (;;) {
        __asm__ volatile("nop");
    }
}
