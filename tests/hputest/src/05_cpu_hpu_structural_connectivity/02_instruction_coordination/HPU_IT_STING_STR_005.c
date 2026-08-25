#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_STING_STR_005"
#define TESTPOINT "IT-STR-005"
#define DESCRIPTION "RISC-V与HPU混合代码段约束随机"
#define TEST_MODE "STING约束随机+功能覆盖"
#define PRIORITY 3
#define CASE_KIND "HPU_CASE_STR_RANDOM_MIXED"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_READY_CONTROL | HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_ENTRY | HPU_REQ_FUNCTION_COVERAGE | HPU_REQ_STING | HPU_REQ_QUEUE_CONTROL"
#define SEED UINT32_C(0xaca935f6)

int main(void) {
    /*
     * Fail closed: the STING-generated mixed RISC-V/HPU code image, replay
     * metadata, x10/x11 DMA relocation spans, architectural result golden,
     * and retirement/queue monitor contract are not connected.  Deterministic
     * PADD/PMUL calls are not a substitute, so issue no HPU command.
     */
    (void)hpu_it_not_issued();
    return 1;
}
