#include <hpu/progress.h>
#include <klib.h>
#include <limits.h>
#include <stdint.h>

#ifdef HPU_PROGRESS_HOST_TEST
/* 仅主机单元测试提供这些 hook，不执行 RISC-V CSR 指令。 */
extern const char *progress_host_mainargs(void);
extern void progress_host_enable_cycle(void);
extern uint64_t progress_host_cycle(void);
#else
extern const char __am_mainargs;
#endif

static unsigned total_subcases;
static unsigned selected_subcase;
static int selected_all;
static int started;
static uint64_t previous_cycle;
static const char *previous_phase;

static const char *mainargs(void) {
#ifdef HPU_PROGRESS_HOST_TEST
    return progress_host_mainargs();
#else
    return &__am_mainargs;
#endif
}

static void enable_cycle(void) {
#ifdef HPU_PROGRESS_HOST_TEST
    progress_host_enable_cycle();
#else
    /* main 从 M 模式进入；此处只置 CY，保留其余 mcounteren 位不变。 */
    __asm__ volatile("csrs mcounteren, %0" : : "r"((uintptr_t)1U) : "memory");
#endif
}

static uint64_t read_cycle(void) {
#ifdef HPU_PROGRESS_HOST_TEST
    return progress_host_cycle();
#else
    uint64_t cycle;
    /* M/S 两种模式使用同一个 cycle CSR，不读取计时器，也不使能中断。 */
    __asm__ volatile("rdcycle %0" : "=r"(cycle) : : "memory");
    return cycle;
#endif
}

static int parse_selection(const char *argument, unsigned count) {
    static const char prefix[] = "subcase=";
    unsigned value = 0U;
    unsigned position = 0U;

    if (argument == NULL || count == 0U) return 1;
    if (argument[0] == '\0' || strcmp(argument, "all") == 0) {
        selected_all = 1;
        selected_subcase = 0U;
        return 0;
    }
    while (prefix[position] != '\0') {
        if (argument[position] != prefix[position]) return 1;
        ++position;
    }
    if (argument[position] == '\0') return 1;
    for (; argument[position] != '\0'; ++position) {
        const char digit = argument[position];
        if (digit < '0' || digit > '9') return 1;
        if (value > (UINT_MAX - (unsigned)(digit - '0')) / 10U) return 1;
        value = value * 10U + (unsigned)(digit - '0');
    }
    if (value >= count) return 1;
    selected_all = 0;
    selected_subcase = value;
    return 0;
}

int progress_begin(const char *case_id, unsigned subcases) {
    const char *argument = mainargs();

    /* 防止后续已进入 S 模式时重复写 M CSR。初始化只允许调用一次。 */
    if (started) {
        printf("[HPU][PROGRESS][FAIL] reason=already-started\n");
        return 1;
    }
    if (case_id == NULL || case_id[0] == '\0' ||
        parse_selection(argument, subcases) != 0) {
        printf("[HPU][PROGRESS][FAIL] reason=invalid-mainargs args=%s "
               "subcases=%u expected=all-or-subcase=N valid_N=0..count-1\n",
               argument != NULL ? argument : "(null)", subcases);
        return 1;
    }
    enable_cycle();
    total_subcases = subcases;
    if (selected_all) {
        printf("[HPU][PROGRESS] case=%s selection=all subcases=%u coverage=full\n",
               case_id, subcases);
    } else {
        printf("[HPU][PROGRESS] case=%s selection=subcase=%u subcases=%u "
               "coverage=selected-subset-not-full\n",
               case_id, selected_subcase, subcases);
    }
    printf("[HPU][PROGRESS] timing=CPU-phase-cycles not=HPU-pure-compute "
           "phase-log-print-excluded=1 other-UART-inside-phase-included=1\n");
    previous_phase = "begin";
    previous_cycle = read_cycle();
    started = 1;
    return 0;
}

int subcase_selected(unsigned index) {
    return started && index < total_subcases &&
        (selected_all || index == selected_subcase);
}

void phase_mark(const char *phase) {
    if (!started || phase == NULL || phase[0] == '\0') {
        printf("[HPU][PHASE][FAIL] reason=invalid-phase-or-not-started\n");
        return;
    }
    const uint64_t now = read_cycle();
    printf("[HPU][PHASE] current=%s cycle=%lu previous=%s cpu_elapsed=%lu\n",
           phase, (unsigned long)now, previous_phase,
           (unsigned long)(now - previous_cycle));
    previous_phase = phase;
    /* 不把本条 phase 日志的 UART 阻塞时间归入下一计算阶段。 */
    previous_cycle = read_cycle();
}
