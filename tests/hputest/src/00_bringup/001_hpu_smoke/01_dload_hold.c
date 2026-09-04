#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>

/*
 * 目的：确认 DLOAD 指令能到达 HPU，并给波形检查留下足够时间。
 * 重点观察命令握手、x10/x11、DMA 地址与长度、AXI 读事务。
 * 这是波形观察用例，成功路径故意不返回。
 */
int main(void) {
    case_start(__FILE__);
    uint32_t status = 0U;
    unsigned timeout;

    /* ELF 中必须完整包含 A/B 两组 4096 个 32 位系数。 */
    if (fixture_validate() != 0) return case_fail(__FILE__, __LINE__);

    /* 清除上一次运行可能留下的 fault 和完成电平。 */
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);

    /* 逐寄存器配置 DDR window，便于直接在波形中定位每次 MMIO。 */
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, SMOKE_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (csr_read(CSR_BASE_LO) != (uint32_t)MEM_BASE) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_BASE_HI) != (uint32_t)(MEM_BASE >> 32U)) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_SIZE_LO) != SMOKE_LINES) return case_fail(__FILE__, __LINE__);
    if (csr_read(CSR_SIZE_HI) != 0U) return case_fail(__FILE__, __LINE__);

    csr_write(CSR_COMMIT, COMMIT);
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_FAULT) != 0U) return case_fail(__FILE__, __LINE__);
        if ((status & STATUS_VALID) != 0U) break;
    }
    if (timeout == TIMEOUT || (status & STATUS_BUSY) != 0U) return case_fail(__FILE__, __LINE__);

    fixture_copy(LINE_A, RNS_A);

    /* p0、line 偏移和 line 数都是参数，调用可直接复用于其他对象。 */
    if (dload(P0, LINE_A, RNS_LINES) != 0) return case_fail(__FILE__, __LINE__);

    /* 故意停在这里；由仿真 cycle limit 结束本用例。 */
    case_hold(__FILE__);
    for (;;) {
        __asm__ volatile("nop");
    }
}
