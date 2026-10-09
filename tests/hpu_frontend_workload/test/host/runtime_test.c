#include <assert.h>
#include <workload.h>

/* 仅验证AM接收端的内存/同步逻辑，不在host上伪装执行HPU。 */
const uint32_t initial_image[IMAGE_LINES * LINE_WORDS] = {
    [0] = 0x12345678U, [64] = UINT32_MAX, [128] = 0xa5a55a5aU
};
const uint32_t golden_words[GOLDEN_WORDS] = { [0] = 5U, [1] = 10U };
const uint8_t writable_lines[IMAGE_LINES] = { 0, 1, 0 };
const struct output_span outputs[OUTPUT_COUNT] = { {1, 64, 0, 17, 0, 0, 0} };
static uint32_t ram[IMAGE_LINES * LINE_WORDS];
static unsigned mode, status_reads, write_count;
static uint64_t cycles;
static uint32_t irq_level;

volatile uint32_t *test_line(unsigned line) { return ram + line * LINE_WORDS; }
uint64_t test_cycle(void) { cycles += 256U; return cycles; }
uint32_t test_csr_read(uintptr_t address) {
    if (address == CSR_IRQ) return irq_level;
    if (address == CSR_STATUS) {
        ++status_reads;
        if (mode == 2U) return STATUS_VALID | STATUS_FAULT;
        if (mode == 0U && status_reads < 3U) return STATUS_VALID | STATUS_BUSY;
        return STATUS_VALID;
    }
    if (address == CSR_FAULT) return mode == 2U ? FAULT_VALID : 0U;
    return 0U;
}
void test_csr_write(uintptr_t address, uint32_t value) {
    assert(address == CSR_IRQ);
    ++write_count;
    if (mode != 4U && value == IRQ_LEVEL) irq_level = 0U;
}

int main(void) {
    prepare_image();
    assert(ram[0] == initial_image[0] && ram[64] == UINT32_MAX && ram[128] == 0xa5a55a5aU);
    /* 未执行DSTORE时，毒化输出必须失败，不能把golden预填造成假PASS。 */
    assert(check_results() == 1);
    for (unsigned i = 0U; i < GOLDEN_WORDS; ++i) ram[64U + i] = golden_words[i];
    workload_trace.errors = 0U;
    assert(check_results() == 0 && check_memory() == 0);
    ram[65] = 18U;
    assert(check_results() == 1 && workload_trace.errors == 1U);
    assert(workload_trace.first_line == 1U && workload_trace.first_word == 1U);
    assert(workload_trace.actual == 18U && workload_trace.expected == 10U);
    ram[65] = golden_words[1];
    ram[0] ^= 1U;
    assert(check_memory() == 1); /* 输入/常量不允许被写。 */
    ram[0] = initial_image[0];
    ram[128] ^= 1U;
    assert(check_memory() == 1); /* 窗口末端guard也必须检查。 */
    ram[128] = initial_image[128];

    irq_level = IRQ_LEVEL;
    mode = 0U;
    assert(wait_program() == 0 && status_reads == 3U); /* IRQ先到、BUSY后清的竞态。 */
    assert(clear_completion() == 0 && irq_level == 0U && write_count == 2U);
    mode = 1U;
    assert(wait_program() == 1); /* idle却没有完成事件，不能提前通过。 */
    mode = 2U;
    assert(wait_program() == 1 && workload_trace.fault == FAULT_VALID);
    mode = 3U;
    assert(wait_window() == 0); /* COMMIT就绪不需要PSYNC事件。 */
    mode = 4U;
    irq_level = IRQ_LEVEL;
    assert(clear_completion() == 1); /* 清电平也必须有cycle超时。 */
    return 0;
}
