#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_STING_CMB_007"
#define TESTPOINT "IT-CMB-007"
#define DESCRIPTION "HPU可执行代码段约束随机排列组合"
#define TEST_MODE "STING约束随机"
#define PRIORITY 3
#define CASE_KIND "HPU_CASE_STING_PROGRAMS"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_DATA | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_STING"
#define SEED UINT32_C(0x8fd40bf5)

int main(void) {
    /*
     * Fail closed: no STING-generated legal HPU program, exact replay seed
     * record, x10/x11 DMA relocation-span manifest, matching input images,
     * or immutable final golden is connected to this receiver.  A fixed
     * PADD/PMUL sample is not evidence for the requested random sequence, so
     * this testcase deliberately issues no HPU command.
     */
    (void)hpu_it_not_issued();
    return 1;
}
