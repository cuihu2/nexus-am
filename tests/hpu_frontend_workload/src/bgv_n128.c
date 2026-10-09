#include <workload.h>
#include <program.h>
#include <hpu/log.h>

/*
 * BGV，N=128：x×(RotateRows(x,1)+RotateColumns(x))+7。
 * 应用图使用新ApplicationBuilder/Evaluator API生成，采用新资源管理布局。
 * 每个ELF仅执行本规模的一张应用图；逐节点raw golden全部检查。
 * 不启用CPU定时器或PLIC：程序末尾唯一PSYNC通过MMIO完成电平同步。
 */
int main(void) {
    _Static_assert(DEGREE == 128U, "wrong frontend degree");
    LOG_EVENT("[FRONTEND] %s N=%u inst=%u dma=%u lines=%u\n",
              CASE_ID, DEGREE, INSTRUCTION_COUNT, DMA_COUNT, WINDOW_LINES);
    phase_mark(PHASE_PREPARE);
    prepare_image();

    /* 先配置shadow寄存器并逐项读回，再COMMIT；错误就返回1。 */
    phase_mark(PHASE_CONFIGURE);
    csr_write(CSR_FAULT, FAULT_VALID);
    /* 确认旧完成电平真正撤销，不能把旧IRQ当成本轮PSYNC完成。 */
    if (clear_completion() != 0) return case_fail(__LINE__);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, WINDOW_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U)) != 0 ||
        expect_csr(CSR_SIZE_LO, WINDOW_LINES) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U) != 0)
        return case_fail(__LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window() != 0) return case_fail(__LINE__);

    /* 直接执行producer的C和resolved DMA spans，不手写或重编码HPU指令。 */
    phase_mark(PHASE_ISSUE);
    const int rc = hpu_run_bgv_evaluator_application();
    if (rc != 0) {
        LOG_ERROR("[FRONTEND][PROGRAM] rc=%d\n", rc);
        return case_fail(__LINE__);
    }
    phase_mark(PHASE_WAIT);
    if (wait_program() != 0 || clear_completion() != 0)
        return case_fail(__LINE__);

    /* 必须全部结果一致且只读/guard未变，才return 0；不打印4096个系数。 */
    phase_mark(PHASE_COMPARE);
    const int data_rc = check_results();
    const int memory_rc = check_memory();
    if (data_rc != 0 || memory_rc != 0) return case_fail(__LINE__);
    return case_pass();
}
