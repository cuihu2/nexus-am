#include <hpu/result.h>
#include <hpu/csr.h>
#include <hpu/dma.h>
#include <hpu/fixture.h>
#include <hpu/layout.h>

/*
 * 目的：仅轮询 MMIO STATUS，完成 DDR -> HPU -> DDR 回环并逐项自检。
 * 不发 PSYNC、不访问 IRQ；两次 DMA 分开确认忙到空闲，避免混淆完成边界。
 * 输出区先写 poison，避免 DSTORE 没执行时误把旧数据当成正确结果。
 */
int main(void) {
    case_start(__FILE__);
    uint32_t status = 0U;
    unsigned timeout;
    int saw_busy = 0;

    if (fixture_validate() != 0) return case_fail(__FILE__, __LINE__);

    csr_write(CSR_FAULT, FAULT_VALID);
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
    fixture_poison();
    /* 打印放在发指令之前，发出后立即轮询，避免 UART 输出拖过忙阶段。 */
    printf("[HPU][PHASE] DLOAD: poll STATUS busy -> idle\n");
    if (dload(P0, LINE_A, RNS_LINES) != 0) return case_fail(__FILE__, __LINE__);

    /*
     * 先确认 DLOAD 实际进入忙状态，再等它结束，之后才发 DSTORE。
     * 启动前的 BUSY=0 不代表完成；未观察到忙阶段时保守超时报错。
     */
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_VALID) == 0U ||
            (status & STATUS_FAULT) != 0U ||
            (csr_read(CSR_FAULT) & FAULT_VALID) != 0U)
            return case_fail(__FILE__, __LINE__);
        if ((status & STATUS_BUSY) != 0U) saw_busy = 1;
        else if (saw_busy) break;
    }
    if (timeout == TIMEOUT) return case_fail(__FILE__, __LINE__);

    printf("[HPU][PHASE] DLOAD idle; DSTORE: poll STATUS busy -> idle\n");
    saw_busy = 0;
    if (dstore(P0, LINE_OUT, RNS_LINES) != 0) return case_fail(__FILE__, __LINE__);

    /* DSTORE 必须重新观察忙到空闲，不能沿用 DLOAD 的 saw_busy 或空闲读值。 */
    for (timeout = 0U; timeout < TIMEOUT; ++timeout) {
        status = csr_read(CSR_STATUS);
        if ((status & STATUS_VALID) == 0U ||
            (status & STATUS_FAULT) != 0U ||
            (csr_read(CSR_FAULT) & FAULT_VALID) != 0U)
            return case_fail(__FILE__, __LINE__);
        if ((status & STATUS_BUSY) != 0U) saw_busy = 1;
        else if (saw_busy) break;
    }
    if (timeout == TIMEOUT) return case_fail(__FILE__, __LINE__);

    /* invalidate 后逐个比较 4096 个系数；任何不一致都 return 1。 */
    if (check_loopback() != 0) return case_fail(__FILE__, __LINE__);
    return case_pass(__FILE__);
}
