#include <hpu/steps.h>

/*
 * 测试点：IT-CMB-007
 * 目的：HPU可执行代码段约束随机排列组合。
 * 模式：STING约束随机（P3）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 *   - HPU_REQ_EXTERNAL_DATA
 *   - HPU_REQ_EXTERNAL_ENTRY
 *   - HPU_REQ_STING
 */

int main(void) {
    /*
     * Fail closed: no STING-generated legal HPU program, exact replay seed
     * record, x10/x11 DMA relocation-span manifest, matching input images,
     * or immutable final golden is connected to this receiver.  A fixed
     * PADD/PMUL sample is not evidence for the requested random sequence, so
     * this testcase deliberately issues no HPU command.
     */
    (void)not_issued();
    return 1;
}
