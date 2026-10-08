#include <hpu/watchdog.h>
#include <hpu/watchdog_data.h>
#include <hpu/edge.h>
#include <hpu/result.h>

/*
 * 测试点：HPU_IT_EDGE_WATCHDOG_COMPUTE_BEFORE_DLOAD
 * 目的：窗口正常提交后，PADD先于源数据DLOAD，验证缺对象依赖导致的入口看门狗。
 * PADD的p0/p1均未alloc/valid，controller按block_reason=6停住。
 * 后排的DLOAD无法越过队首命令补齐依赖；再发64条命令填满下游队列。
 * 这是针对two_core_no_fdi@ff86d9e的依赖死锁注入，不验收非法计算结果。
 * 编码、对象号、DDR地址和长度合法，故意违规的是指令依赖顺序。
 * 每个ELF单独复位；预期FAULT code=1、CPU可结束、无HPU写回和完成IRQ。
 * 仅程序末尾一次PSYNC，fail-stop下它必须被丢弃；不用PLIC或timer。
 */
int main(void) {
    enum { lines = 4, words = lines * WORDS_PER_LINE };
    uint32_t expected[words], status, fault;
    uint64_t begin, elapsed;
    case_start(__FILE__);
    trace_phase(TRACE_PREPARE);
    if ((csr_read(CSR_STATUS) & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) != 0U ||
        (csr_read(CSR_FAULT) & FAULT_VALID) != 0U ||
        (csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U)
        return edge_failure(__FILE__, __LINE__, "requires-fresh-reset");
    if (wd_prepare_data((uintptr_t)MEM_BASE, expected, words) != 0)
        return edge_failure(__FILE__, __LINE__, "fixture");

    /* 正常配置窗口，排除旧用例的“未COMMIT”阻塞原因。 */
    trace_phase(TRACE_CONFIGURE);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, lines);
    csr_write(CSR_SIZE_HI, 0U);
    if (csr_read(CSR_BASE_LO) != (uint32_t)MEM_BASE ||
        csr_read(CSR_BASE_HI) != (uint32_t)(MEM_BASE >> 32U) ||
        csr_read(CSR_SIZE_LO) != lines || csr_read(CSR_SIZE_HI) != 0U)
        return edge_failure(__FILE__, __LINE__, "shadow-readback");
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0)
        return edge_failure(__FILE__, __LINE__, "commit");

    LOG_EVENT("[HPU][WATCHDOG][DEPENDENCY] scenario=compute-before-dload commands=69 expected_cpu_cycles=%lu\n",
              (unsigned long)WD_CYCLES);
    trace_phase(TRACE_ISSUE);
    begin = wd_cycle();
    /* 整段汇编仅用producer编码；无函数调用/MMIO/UART，DLOAD offset显式递增。 */
    {
        register uintptr_t offset __asm__("x10") = 0U;
        register uintptr_t length __asm__("x11") = 1U;
        __asm__ volatile(
            ".global watchdog_order_begin\nwatchdog_order_begin:\n"
            ".word %2\n" /* 先计算：p0/p1尚未DLOAD。 */
            ".word %3\naddi %0, %0, 1\n.word %4\n"
            "addi %0, %0, 1\n.word %5\n.word %6\n"
            ".rept %7\n.word %2\n.endr\n"
            ".global watchdog_order_end\nwatchdog_order_end:\n"
            : "+r"(offset)
            : "r"(length), "i"((uint32_t)HPU_INSN_PADD_P0_P0_P1),
              "i"((uint32_t)HPU_INSN_DLOAD_P0_POLY),
              "i"((uint32_t)HPU_INSN_DLOAD_P1_POLY),
              "i"((uint32_t)HPU_INSN_DLOAD_P4_MOD),
              "i"((uint32_t)HPU_INSN_PMODLD_0), "i"(64) : "memory");
    }
    elapsed = wd_cycle() - begin;
    application_trace.wait_cycles = elapsed;

    /* CPU能走到这里是看门狗释放Fence的结果；不等于HPU命令正常完成。 */
    trace_phase(TRACE_WAIT);
    begin = wd_cycle();
    for (;;) {
        fault = csr_read(CSR_FAULT);
        status = csr_read(CSR_STATUS);
        application_trace.fault = fault;
        application_trace.status = status;
        if ((fault & FAULT_VALID) != 0U && fault_code(fault) != WD_CODE)
            return edge_failure(__FILE__, __LINE__, "wrong-fault-code");
        if (wd_fault_expected(status, fault, 1U)) break;
        if (wd_cycle() - begin >= UINT64_C(100000))
            return edge_failure(__FILE__, __LINE__, "missing-dependency-timeout");
    }
    LOG_EVENT("[HPU][WATCHDOG][DEPENDENCY] status=0x%x fault=0x%x code=%u issue_cycles=%lu\n",
              status, fault, fault_code(fault), (unsigned long)elapsed);

    psync(); /* 唯一末尾PSYNC；不等待一个本应被丢弃的完成事件。 */
    status = csr_read(CSR_STATUS);
    fault = csr_read(CSR_FAULT);
    application_trace.irq = csr_read(CSR_IRQ);
    if (!wd_fault_expected(status, fault, 1U) ||
        (application_trace.irq & IRQ_LEVEL) != 0U)
        return edge_failure(__FILE__, __LINE__, "unexpected-completion-or-fault");
    trace_phase(TRACE_GUARD);
    if (wd_check_memory((uintptr_t)MEM_BASE, expected, words) != 0)
        return edge_failure(__FILE__, __LINE__, "unexpected-ddr-write");
    trace_phase(TRACE_DONE);
    return case_pass(__FILE__);
}
