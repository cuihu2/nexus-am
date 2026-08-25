#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_INS_C0_005"
#define TESTPOINT "IT-INS-C0-005"
#define DESCRIPTION "PNTT单stage结果写回闭环"
#define TEST_MODE "定向单stage"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_INS_PNTT_STAGE"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
#define SEED UINT32_C(0xc005)

int main(void) {
    /*
     * Fail closed: the producer supplies a PNTT instruction word, but this
     * receiver does not yet have the matching N=4096 stage input layout,
     * twiddle/modulus memory image, resolved DMA spans, and immutable
     * per-stage golden output as one versioned delivery.  Issuing the word
     * against the placeholder twiddle line would not test IT-INS-C0-005.
     */
    (void)hpu_it_not_issued();
    return 1;
}
