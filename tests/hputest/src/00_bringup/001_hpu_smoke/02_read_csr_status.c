#include <hpu/fixture.h>
#include <hpu/init.h>
#include <hpu/result.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_02_READ_CSR_STATUS";
    int rc;

    hpu_test_begin(case_id, "read-csr-status");

    if (hpu_fixture_validate_embedded() != 0) {
        return hpu_test_fail(case_id, "fixture", 1U);
    }
    rc = hpu_initialize_and_verify();
    if (rc != 0) return hpu_test_fail(case_id, "hpu-init", (unsigned)rc);
    return hpu_test_end(case_id, 0);
}
