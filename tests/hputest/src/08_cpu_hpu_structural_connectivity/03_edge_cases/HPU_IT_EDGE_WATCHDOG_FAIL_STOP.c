#include <hpu/watchdog.h>
#include <hpu/fixture.h>
#include <hpu/encoding.h>
#include <hpu/sync.h>
#include <hpu/trace.h>
#include <hpu/result.h>
#include <hpu/edge.h>

/*
 * 测试点：HPU_IT_EDGE_WATCHDOG_FAIL_STOP（预期故障的负向自检，不是计算用例）。
 * 目的：验证入口握手超时故障、CPU继续运行、sticky停发和无DDR写入。
 * 适用：IT-SCPU-RTL/two_core_no_fdi，至少包含ff86d9e的CPU入口看门狗及故障码。
 * 每个ELF必须独立复位：不提交窗口，controller按block_reason=8阻塞DLOAD。
 * 连发64条合法编码、长度为1line的DLOAD，填满下游队列后制造持续入口反压。
 * 期望：500000个CPU时钟后fail-stop；CSR_FAULT有效且code=1，CPU仍可继续。
 * W1C不能解除核侧fail-stop；后续custom1和末尾唯一PSYNC均不应转发。
 * 这里没有RISC-V trap，也不等待PLIC中断；return 0表示预期故障检查通过。
 * 精确计时、接受数量和停发波形仍由IT monitor验收；不得以软件PASS替代。
 */
static uint64_t cycle_now(void) {
    uint64_t value;
    __asm__ volatile("rdcycle %0" : "=r"(value) : : "memory");
    return value;
}

static int check_window(uintptr_t address, const uint32_t *expected, unsigned words) {
    volatile const uint32_t *actual = (volatile const uint32_t *)address;
    hpu_cache_invalidate(address, (size_t)words * sizeof(uint32_t));
    for (unsigned word = 0U; word < words; ++word) {
        const uint32_t value = actual[word];
        if (value != expected[word]) {
            LOG_ERROR("[HPU][WATCHDOG][GUARD-FAIL] word=%u actual=0x%x expected=0x%x\n",
                      word, value, expected[word]);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    enum { lines = 4, commands = 64, words = lines * WORDS_PER_LINE };
    uint32_t expected[words];
    uint32_t status, fault;
    uint64_t begin, elapsed;
    volatile uint32_t *memory = line_ptr(0U);

    case_start(__FILE__);
    trace_phase(TRACE_PREPARE);
    /* 不能继承上一个ELF的窗口或sticky fault，否则无法隔离此次计时。 */
    if ((csr_read(CSR_STATUS) & (STATUS_VALID | STATUS_BUSY | STATUS_FAULT)) != 0U ||
        (csr_read(CSR_FAULT) & FAULT_VALID) != 0U ||
        (csr_read(CSR_IRQ) & IRQ_LEVEL) != 0U)
        return edge_failure(__FILE__, __LINE__, "requires-fresh-reset");

    /* A/B来自inline-asm嵌入的4096系数输入；本例各取首line，其余两line为guard。 */
    for (unsigned word = 0U; word < words; ++word) {
        expected[word] = word < WORDS_PER_LINE ? RNS_A[word] :
                         word < 2U * WORDS_PER_LINE ? RNS_B[word - WORDS_PER_LINE] :
                         UINT32_C(0xa5c30000) ^ word;
        memory[word] = expected[word];
    }
    mem_fence();
    hpu_cache_clean((uintptr_t)memory, sizeof(expected));

    trace_phase(TRACE_CONFIGURE);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, lines);
    csr_write(CSR_SIZE_HI, 0U);
    if (csr_read(CSR_BASE_LO) != (uint32_t)MEM_BASE ||
        csr_read(CSR_BASE_HI) != (uint32_t)(MEM_BASE >> 32U) ||
        csr_read(CSR_SIZE_LO) != lines || csr_read(CSR_SIZE_HI) != 0U)
        return edge_failure(__FILE__, __LINE__, "shadow-readback");
    /* 故意不写COMMIT：这是已核实RTL的阻塞刺激，不能套用正向初始化流程。 */
    if ((csr_read(CSR_STATUS) & STATUS_VALID) != 0U)
        return edge_failure(__FILE__, __LINE__, "unexpected-window-valid");

    LOG_EVENT("[HPU][WATCHDOG] stall DLOAD count=%u expected_cpu_cycles=%lu\n",
              commands, (unsigned long)WD_CYCLES);
    trace_phase(TRACE_ISSUE);
    begin = cycle_now();
    {
        register uintptr_t offset __asm__("x10") = 0U;
        register uintptr_t length __asm__("x11") = 1U;
        __asm__ volatile(
            ".global watchdog_stall_begin\nwatchdog_stall_begin:\n"
            ".rept %3\n.word %2\n.endr\n"
            ".global watchdog_stall_end\nwatchdog_stall_end:\n"
            : : "r"(offset), "r"(length),
                "i"((uint32_t)HPU_INSN_DLOAD_P0_POLY), "i"(commands) : "memory");
    }
    elapsed = cycle_now() - begin;
    application_trace.wait_cycles = elapsed;

    /* fault和status分别跨域，不把两次MMIO读视作原子快照。 */
    trace_phase(TRACE_WAIT);
    begin = cycle_now();
    for (;;) {
        fault = csr_read(CSR_FAULT);
        status = csr_read(CSR_STATUS);
        application_trace.fault = fault;
        application_trace.status = status;
        if ((fault & FAULT_VALID) != 0U && fault_code(fault) != WD_CODE)
            return edge_failure(__FILE__, __LINE__, "wrong-fault-code");
        if (watchdog_fault_match(status, fault)) break;
        if (cycle_now() - begin >= UINT64_C(100000))
            return edge_failure(__FILE__, __LINE__, "missing-cpu-timeout-fault");
    }
    LOG_EVENT("[HPU][WATCHDOG] expected fault status=0x%x fault=0x%x code=%u issue_cycles=%lu\n",
              status, fault, fault_code(fault), (unsigned long)elapsed);

    /* 故障源保持置位。清MMIO记录不能使CPU重新向HPU发令，也不能重启计时。 */
    csr_write(CSR_FAULT, FAULT_VALID);
    begin = cycle_now();
    {
        register uintptr_t offset __asm__("x10") = 1U;
        register uintptr_t length __asm__("x11") = 1U;
        __asm__ volatile(
            ".global watchdog_drop_begin\nwatchdog_drop_begin:\n"
            ".rept %3\n.word %2\n.endr\n"
            ".global watchdog_drop_end\nwatchdog_drop_end:\n"
            : : "r"(offset), "r"(length),
                "i"((uint32_t)HPU_INSN_DLOAD_P1_POLY), "i"(commands) : "memory");
    }
    psync(); /* 完整负向程序的唯一PSYNC；fail-stop下它也必须被丢弃。 */
    elapsed = cycle_now() - begin;
    if (elapsed >= WD_CYCLES)
        return edge_failure(__FILE__, __LINE__, "watchdog-rearmed-after-w1c");

    /* 留出观察区间再检查；此延时不是W1C完成ack，重放仍需IT monitor取证。 */
    begin = cycle_now();
    while (cycle_now() - begin < UINT64_C(10000)) {
        __asm__ volatile("nop");
    }
    status = csr_read(CSR_STATUS);
    fault = csr_read(CSR_FAULT);
    application_trace.irq = csr_read(CSR_IRQ);
    if (!watchdog_fault_match(status, fault) || (application_trace.irq & IRQ_LEVEL) != 0U)
        return edge_failure(__FILE__, __LINE__, "fail-stop-not-sticky");

    trace_phase(TRACE_GUARD);
    if (check_window((uintptr_t)memory, expected, words) != 0)
        return edge_failure(__FILE__, __LINE__, "unexpected-ddr-write");
    LOG_EVENT("[HPU][WATCHDOG] post_fault_cycles=%lu guard_words=%u recovery=reset\n",
              (unsigned long)elapsed, words);
    trace_phase(TRACE_DONE);
    return case_pass(__FILE__);
}
