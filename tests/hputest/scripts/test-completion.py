#!/usr/bin/env python3
"""用主机编译器运行实际 completion.h，验证 MMIO 完成事件的状态处理。

另外编译实际 09 用例，替换硬件访问和数据接口，检查其 PSYNC 完成轮询。
06 的纯 STATUS 同步由 test-dma-poll.py 单独验证。
不模拟 RISC-V、PLIC、HPU 运算或实际跨时钟域时序。
"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


TEST_ROOT = Path(__file__).resolve().parents[1]
MOCK_CSR = r"""
#include <stdint.h>
#define CSR_STATUS 0x08000014U
#define CSR_FAULT  0x08000018U
#define CSR_IRQ    0x0800001cU
#define STATUS_VALID 1U
#define STATUS_BUSY  2U
#define STATUS_FAULT 4U
#define FAULT_VALID 1U
#define IRQ_LEVEL 1U
#ifndef TIMEOUT
#define TIMEOUT 8U
#endif
uint32_t csr_read(uintptr_t address);
void csr_write(uintptr_t address, uint32_t value);
"""

HARNESS = r"""
#include <assert.h>
#include <stdlib.h>
#include <hpu/completion.h>

static uint32_t status = STATUS_VALID, fault, level;
static unsigned reads, done_at, clear_at, writes, idle_at, fault_at;
static int clearing, stay_busy, late_fault;
static int status_fault;
static uint32_t written[8];

uint32_t csr_read(uintptr_t address) {
    if (address == CSR_STATUS) return status;
    if (address == CSR_FAULT) return fault;
    assert(address == CSR_IRQ);
    ++reads;
    if (clearing) {
        if (clear_at != 0U && reads >= clear_at) level = 0U;
    } else if (done_at != 0U && reads >= done_at) {
        level = IRQ_LEVEL;
        if (!stay_busy && (idle_at == 0U || reads >= idle_at))
            status &= ~STATUS_BUSY;
        if (late_fault) fault = FAULT_VALID;
    }
    if (!clearing && fault_at != 0U && reads >= fault_at) {
        if (status_fault) status |= STATUS_FAULT;
        else fault = FAULT_VALID;
    }
    return level;
}

void csr_write(uintptr_t address, uint32_t value) {
    assert(address == CSR_IRQ);
    assert(writes < sizeof(written) / sizeof(written[0]));
    written[writes++] = value;
    if (value == IRQ_LEVEL) {
        clearing = 1;
        reads = 0U;
    } else {
        assert(value == 0U);
        clearing = 0;
        done_at = 0U;
    }
}

static void expect_clear_writes(void) {
    assert(writes == 2U);
    assert(written[0] == IRQ_LEVEL && written[1] == 0U);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    switch (atoi(argv[1])) {
    case 1: /* HPU 空闲不能替代 PSYNC 完成通知。 */
        assert(completion_wait() == 1);
        assert(reads == TIMEOUT);
        assert(writes == 0U);
        break;
    case 2: /* 首次读到 busy，随后 IRQ 到达，必须重新读取状态。 */
        status |= STATUS_BUSY;
        done_at = 3U;
        assert(completion_wait() == 0);
        assert(reads == 3U && level == IRQ_LEVEL);
        assert(writes == 0U);
        break;
    case 3: /* STATUS 中的 fault 立即失败。 */
        status |= STATUS_FAULT;
        assert(completion_wait() == 1 && reads == 0U);
        break;
    case 4: /* 独立 FAULT 寄存器也必须参与检查。 */
        fault = FAULT_VALID;
        assert(completion_wait() == 1 && reads == 0U);
        break;
    case 5: /* IRQ 有效但 BUSY 永不清零，应读满超时，不能提前失败。 */
        status |= STATUS_BUSY;
        stay_busy = 1;
        done_at = 1U;
        assert(completion_wait() == 1);
        assert(reads == TIMEOUT);
        break;
    case 6: /* 没有有效窗口，即使有 IRQ 也不能通过。 */
        status = 0U;
        done_at = 1U;
        assert(completion_wait() == 1);
        break;
    case 7: /* IRQ 到达后才出现的 fault 也必须重新检查。 */
        done_at = 1U;
        late_fault = 1;
        assert(completion_wait() == 1);
        break;
    case 8: /* W1C 经多次读取后生效。 */
        level = IRQ_LEVEL;
        clear_at = 3U;
        assert(completion_clear() == 0);
        assert(reads == 3U && level == 0U);
        expect_clear_writes();
        break;
    case 9: /* 完成电平清不掉，必须有界失败。 */
        level = IRQ_LEVEL;
        assert(completion_clear() == 1);
        assert(reads == TIMEOUT && level == IRQ_LEVEL);
        expect_clear_writes();
        break;
    case 10: /* 清 IRQ 成功不能掩盖独立 FAULT。 */
        level = IRQ_LEVEL;
        clear_at = 1U;
        fault = FAULT_VALID;
        assert(completion_clear() == 1);
        expect_clear_writes();
        break;
    case 11: /* 第一阶段通知消费后，第二阶段必须等待新通知。 */
        done_at = 1U;
        assert(completion_wait() == 0);
        clear_at = 2U;
        assert(completion_clear() == 0);
        expect_clear_writes();
        reads = 0U;
        assert(completion_wait() == 1);
        assert(reads == TIMEOUT && level == 0U);
        reads = 0U;
        done_at = 2U;
        assert(completion_wait() == 0);
        assert(reads == 2U);
        break;
    case 12: /* 清 IRQ 成功也不能掩盖 STATUS 的 fault。 */
        level = IRQ_LEVEL;
        clear_at = 1U;
        status |= STATUS_FAULT;
        assert(completion_clear() == 1);
        expect_clear_writes();
        break;
    case 13: /* IRQ 先到，BUSY 多轮后才清零，必须继续轮询并成功。 */
        status |= STATUS_BUSY;
        done_at = 1U;
        idle_at = 4U;
        assert(completion_wait() == 0);
        assert(reads == 4U && writes == 0U);
        break;
    case 14: /* 等待 BUSY 清零期间出现 STATUS fault，应立即失败。 */
    case 15: /* 等待 BUSY 清零期间出现独立 fault，应立即失败。 */
        status |= STATUS_BUSY;
        stay_busy = 1;
        done_at = 1U;
        fault_at = 3U;
        status_fault = atoi(argv[1]) == 14;
        assert(completion_wait() == 1);
        assert(reads == 3U && writes == 0U);
        break;
    case 16: /* BUSY 在最后一轮清零，仍处于合法等待预算内。 */
        status |= STATUS_BUSY;
        done_at = 1U;
        idle_at = TIMEOUT;
        assert(completion_wait() == 0);
        assert(reads == TIMEOUT);
        break;
    case 17: /* IRQ 尚未到达时出现独立 fault，也不能等到超时。 */
        fault_at = 2U;
        assert(completion_wait() == 1);
        assert(reads == 2U && writes == 0U);
        break;
    default:
        abort();
    }
    return 0;
}
"""

MOCK_CASE_API = r"""
#ifndef MOCK_CASE_API_H
#define MOCK_CASE_API_H
#include <stdint.h>
#include <stdio.h>
#include <hpu/csr.h>
#define CSR_BASE_LO 0x08000000U
#define CSR_BASE_HI 0x08000004U
#define CSR_SIZE_LO 0x08000008U
#define CSR_SIZE_HI 0x0800000cU
#define CSR_COMMIT  0x08000010U
#define COMMIT 1U
#define MEM_BASE UINT64_C(0x80000000)
#define SMOKE_LINES 256U
#define LINE_MOD 0U
#define LINE_A 1U
#define LINE_B 65U
#define LINE_OUT 129U
#define RNS_LINES 64U
#define P0 0U
#define RNS_A ((const uint32_t *)(uintptr_t)1U)
#define RNS_B ((const uint32_t *)(uintptr_t)2U)
#define HPU_PROGRAM_MM_DMA_COUNT 4U
typedef struct { uint32_t line; uint32_t length; } hpu_dma_span_t;
void case_start(const char *file);
int case_fail(const char *file, unsigned line);
int case_pass(const char *file);
int fixture_validate(void);
int fixture_validate_mm(void);
void fixture_copy(unsigned line, const uint32_t *data);
void fixture_copy_mod(void);
void fixture_poison(void);
int check_loopback(void);
int check_pmul(void);
int dload(unsigned object, unsigned line, unsigned length);
int dstore(unsigned object, unsigned line, unsigned length);
void psync(void);
int mm_load_mod(const hpu_dma_span_t *spans, unsigned count);
int mm_compute(const hpu_dma_span_t *spans, unsigned count);
#endif
"""

CASE_HARNESS = r"""
#include <assert.h>
#include <stdlib.h>
#include "mock_case_api.h"

/* 直接包含仓库用例；不提取、不重写被测 main 的轮询代码。 */
#define main smoke_main
#include SMOKE_SOURCE
#undef main

static unsigned scenario, phase, polls, checked;
static uint32_t config[4], status = STATUS_VALID, fault, irq;
static int clearing;

uint32_t csr_read(uintptr_t address) {
    if (address < CSR_COMMIT)
        return config[(address - CSR_BASE_LO) / sizeof(uint32_t)];
    if (address == CSR_STATUS) return status;
    if (address == CSR_FAULT) return fault;
    assert(address == CSR_IRQ);
    if (clearing) return 0U;
    if (phase == 1U) return IRQ_LEVEL; /* 09 的模表阶段先正常完成。 */
    if (phase != 2U) return 0U;

    ++polls;
    if (scenario != 4U) irq = IRQ_LEVEL;
    /* IRQ 读取触发状态变化，可复现先读 STATUS 后读 IRQ 的旧值竞态。 */
    if (scenario == 1U || scenario == 8U ||
        (scenario == 2U && polls >= 4U))
        status &= ~STATUS_BUSY;
    if (polls >= 3U && scenario == 5U) status |= STATUS_FAULT;
    if (polls >= 3U && scenario == 6U) fault = FAULT_VALID;
    return irq;
}

void csr_write(uintptr_t address, uint32_t value) {
    if (address < CSR_COMMIT) {
        config[(address - CSR_BASE_LO) / sizeof(uint32_t)] = value;
    } else if (address == CSR_COMMIT) {
        status = STATUS_VALID;
    } else if (address == CSR_FAULT) {
        fault = 0U;
    } else {
        assert(address == CSR_IRQ);
        assert(value == IRQ_LEVEL || value == 0U);
        clearing = value == IRQ_LEVEL;
        irq = 0U;
        if (value == 0U) phase = 0U;
    }
}

void case_start(const char *file) { (void)file; }
int case_fail(const char *file, unsigned line) {
    (void)file;
    (void)line;
    return 1;
}
int case_pass(const char *file) { (void)file; return 0; }
int fixture_validate(void) { return 0; }
int fixture_validate_mm(void) { return 0; }
void fixture_copy(unsigned line, const uint32_t *data) {
    (void)line;
    (void)data;
}
void fixture_copy_mod(void) {}
void fixture_poison(void) {}
int check_loopback(void) { ++checked; return scenario == 8U; }
int check_pmul(void) { ++checked; return scenario == 8U; }
int dload(unsigned object, unsigned line, unsigned length) {
    (void)object;
    (void)line;
    (void)length;
    return 0;
}

static void start_final_phase(void) {
    phase = 2U;
    status = STATUS_VALID | STATUS_BUSY;
    if (scenario == 4U) status = STATUS_VALID;
    if (scenario == 7U) status = 0U;
}

int dstore(unsigned object, unsigned line, unsigned length) {
    (void)object;
    (void)line;
    (void)length;
    start_final_phase();
    return 0;
}
void psync(void) {
    if (phase != 2U) {
        phase = 1U;
        status = STATUS_VALID;
    }
}
int mm_load_mod(const hpu_dma_span_t *spans, unsigned count) {
    (void)spans;
    assert(count == HPU_PROGRAM_MM_DMA_COUNT);
    return 0;
}
int mm_compute(const hpu_dma_span_t *spans, unsigned count) {
    (void)spans;
    assert(count == HPU_PROGRAM_MM_DMA_COUNT);
    start_final_phase();
    return 0;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = (unsigned)atoi(argv[1]);
    assert(scenario >= 1U && scenario <= 8U);
    int expected = scenario == 1U || scenario == 2U ? 0 : 1;
    int result = smoke_main();
    assert(result == expected);
    if (scenario == 1U || scenario == 8U) assert(polls == 1U);
    if (scenario == 2U) assert(polls == 4U);
    if (scenario == 3U || scenario == 4U) assert(polls == TIMEOUT);
    if (scenario == 5U || scenario == 6U) assert(polls == 3U);
    /* 未确认完成时不能进入数据比较；比较失败也不能报 PASS。 */
    assert(checked == (unsigned)(scenario == 1U || scenario == 2U ||
                                 scenario == 8U));
    return 0;
}
"""


class CompletionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = TEST_ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="completion-host-", dir=build)
        cls.addClassCleanup(cls.tmp.cleanup)
        root = Path(cls.tmp.name)
        (root / "hpu").mkdir()
        (root / "hpu" / "csr.h").write_text(MOCK_CSR, encoding="utf-8")
        source = root / "test.c"
        source.write_text(HARNESS, encoding="utf-8")
        cls.binary = root / "test"
        subprocess.run(
            shlex.split(os.environ.get("HOST_CC", "cc"))
            + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(root), "-I", str(TEST_ROOT / "include"),
               str(source), "-o", str(cls.binary)],
            check=True,
        )
        (root / "mock_case_api.h").write_text(MOCK_CASE_API, encoding="utf-8")
        for header in ("result", "dma", "fixture", "layout", "sync"):
            (root / "hpu" / f"{header}.h").write_text(
                '#include "mock_case_api.h"\n', encoding="utf-8")
        (root / "mm_phases.h").write_text(
            '#include "mock_case_api.h"\n', encoding="utf-8")
        case_source = root / "case_test.c"
        case_source.write_text(CASE_HARNESS, encoding="utf-8")
        cls.case_binaries = []
        smoke = TEST_ROOT / "src" / "00_bringup" / "001_hpu_smoke"
        for name in ("09_dload_compute_dstore_poll_mmio",):
            binary = root / name
            subprocess.run(
                shlex.split(os.environ.get("HOST_CC", "cc"))
                + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-DTIMEOUT=32U", f'-DSMOKE_SOURCE="{smoke / (name + ".c")}"',
                   "-I", str(root), "-I", str(TEST_ROOT / "include"),
                   str(case_source), "-o", str(binary)],
                check=True,
            )
            cls.case_binaries.append(binary)

    def test_mmio_completion_scenarios(self):
        for scenario in range(1, 18):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.binary), str(scenario)], check=True)

    def test_actual_smoke_case_polling(self):
        for binary in self.case_binaries:
            for scenario in range(1, 9):
                with self.subTest(case=binary.name, scenario=scenario):
                    subprocess.run([str(binary), str(scenario)], check=True)


if __name__ == "__main__":
    unittest.main()
