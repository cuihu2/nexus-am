#include <hpu/steps.h>

/*
 * 测试点：IT-INS-C0-006
 * 目的：PINTT单stage结果写回闭环。
 * 模式：定向单stage（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    /*
     * Fail closed: the producer supplies a PINTT instruction word, but the
     * inverse-stage input layout, inverse twiddle/scaling image, resolved DMA
     * spans, and immutable N=4096 stage golden are not connected here.  No
     * HPU command is issued until those assets share one producer revision.
     */
    (void)not_issued();
    return 1;
}
