#include <hpu/result.h>
#include <hpu/steps.h>

/*
 * 测试点：IT-INS-C0-005
 * 目的：PNTT单stage结果写回闭环。
 * 模式：定向单stage（P0）。
 * 外部条件：
 *   - HPU_REQ_IT_MONITOR
 *   - HPU_REQ_CACHE_CONTRACT
 */

int main(void) {
    case_start(__FILE__);
    /*
     * Fail closed: the producer supplies a PNTT instruction word, but this
     * receiver does not yet have the matching N=4096 stage input layout,
     * twiddle/modulus memory image, resolved DMA spans, and immutable
     * per-stage golden output as one versioned delivery.  Issuing the word
     * against the placeholder twiddle line would not test IT-INS-C0-005.
     */
    (void)not_issued();
    case_not_qualified(__FILE__);
    return 1;
}
