#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_INS_C0_006"
#define TESTPOINT "IT-INS-C0-006"
#define DESCRIPTION "PINTT单stage结果写回闭环"
#define TEST_MODE "定向单stage"
#define PRIORITY 0
#define CASE_KIND "HPU_CASE_INS_PINTT_STAGE"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT"
#define SEED UINT32_C(0xc006)

int main(void) {
    /*
     * Fail closed: the producer supplies a PINTT instruction word, but the
     * inverse-stage input layout, inverse twiddle/scaling image, resolved DMA
     * spans, and immutable N=4096 stage golden are not connected here.  No
     * HPU command is issued until those assets share one producer revision.
     */
    (void)hpu_it_not_issued();
    return 1;
}
