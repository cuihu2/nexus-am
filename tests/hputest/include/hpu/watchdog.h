#ifndef HPU_WATCHDOG_H
#define HPU_WATCHDOG_H

#include <hpu/csr.h>

/* IT-SCPU-RTL/two_core_no_fdi@ff86d9e：CPU命令入口连续反压计时。 */
#define WD_CYCLES UINT64_C(500000)
#define WD_CODE 1U
#define WD_CODE_SHIFT 8U
#define WD_FAULT_MASK UINT32_C(0x0000ff73)
#define WD_FAULT_VALUE ((WD_CODE << WD_CODE_SHIFT) | FAULT_VALID)

static inline unsigned fault_code(uint32_t fault) {
    return (unsigned)((fault >> WD_CODE_SHIFT) & 0xffU);
}

/* 窗口未提交、DMA未启动；只能接受CPU入口超时，不能把DMA/越界fault算成功。 */
static inline int watchdog_fault_match(uint32_t status, uint32_t fault) {
    return (status & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) == STATUS_FAULT &&
           (fault & WD_FAULT_MASK) == WD_FAULT_VALUE;
}

#endif
