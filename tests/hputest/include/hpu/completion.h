#ifndef HPU_COMPLETION_H
#define HPU_COMPLETION_H

#include <hpu/csr.h>

/* 仅等待当前PSYNC的MMIO完成通知，不发指令，也不修改完成电平。 */
static inline int completion_wait(void) {
    for (unsigned timeout = 0U; timeout < TIMEOUT; ++timeout) {
        uint32_t status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U ||
            (csr_read(CSR_FAULT) & FAULT_VALID) != 0U)
            return 1;
        if ((csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U) {
            /* 收到完成后重新采样状态，不能把通知到达前的busy读值当成结果。 */
            status = csr_read(CSR_STATUS);
            return (status & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) !=
                       STATUS_VALID ||
                   (csr_read(CSR_FAULT) & FAULT_VALID) != 0U;
        }
    }
    return 1;
}

/* 消费本轮通知后，才允许提交下一阶段；超时或fault都返回失败。 */
static inline int completion_clear(void) {
    unsigned timeout;
    csr_write(CSR_IRQ, IRQ_LEVEL);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        if ((csr_read(CSR_IRQ) & IRQ_LEVEL) == 0U) break;
    }
    csr_write(CSR_IRQ, 0U);
    if (timeout == TIMEOUT) return 1;
    return (csr_read(CSR_STATUS) & STATUS_FAULT) != 0U ||
           (csr_read(CSR_FAULT) & FAULT_VALID) != 0U;
}

#endif
