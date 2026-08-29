#include <hpu/steps.h>

/*
 * 测试点：IT-STR-005
 * 目的：RISC-V与HPU混合代码段约束随机。
 * 模式：STING约束随机+功能覆盖（P3）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_READY_CONTROL
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_EXTERNAL_ENTRY
 *   - HPU_REQ_FUNCTION_COVERAGE
 *   - HPU_REQ_STING
 *   - HPU_REQ_QUEUE_CONTROL
 */

int main(void) {
    /*
     * Fail closed: the STING-generated mixed RISC-V/HPU code image, replay
     * metadata, x10/x11 DMA relocation spans, architectural result golden,
     * and retirement/queue monitor contract are not connected.  Deterministic
     * PADD/PMUL calls are not a substitute, so issue no HPU command.
     */
    (void)not_issued();
    return 1;
}
