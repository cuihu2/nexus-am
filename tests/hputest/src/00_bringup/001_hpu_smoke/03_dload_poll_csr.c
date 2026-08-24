#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/init.h>
#include <hpu/result.h>

int main(void) {
    const char *case_id = "HPU_SMOKE_001_03_DLOAD_POLL_CSR";
    unsigned timeout;
    int saw_busy = 0;
    int rc;

    hpu_test_begin(case_id, "dload+poll-status-busy");

    if (hpu_fixture_validate_embedded() != 0) {
        return hpu_test_fail(case_id, "fixture", 1U);
    }
    rc = hpu_initialize_and_verify();
    if (rc != 0) return hpu_test_fail(case_id, "hpu-init", (unsigned)rc);
    hpu_fixture_copy_inputs_to_ddr();

    /* Polling STATUS.busy proves that the configured window serves DLOAD. */
    hpu_dload_p0(HPU_LINE_SRC_A, HPU_RNS_LINES);
    for (timeout = 0U; timeout < HPU_TIMEOUT; ++timeout) {
        uint32_t status = hpu_csr_read(HPU_CSR_STATUS);
        if ((status & HPU_STATUS_FAULT_VALID) != 0U) {
            return hpu_test_fail(case_id, "dload-fault", 1U);
        }
        if ((status & HPU_STATUS_BUSY) != 0U) saw_busy = 1;
        if (saw_busy && (status & HPU_STATUS_BUSY) == 0U) {
            printf("HPU_SMOKE_CSR_POLL busy_seen=1 busy_done=1 polls=%u\n",
                   timeout + 1U);
            return hpu_test_end(case_id, 0);
        }
    }

    printf("HPU_SMOKE_CSR_POLL busy_seen=%d busy_done=0\n", saw_busy);
    return hpu_test_fail(case_id, "dload-busy-timeout",
                         saw_busy ? 1U : 2U);
}
