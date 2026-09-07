#!/usr/bin/env python3
"""编译实际 06 用例，验证不依赖 PSYNC/IRQ 的两阶段 STATUS 轮询。

主机桩按读取次数提供 idle/busy 序列，检查等待、错误分支及自检顺序。
这不是 HPU、RISC-V 或跨时钟域仿真，不能替代 IT 波形验证。
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


TEST_ROOT = Path(__file__).resolve().parents[1]
SMOKE_SOURCE = (TEST_ROOT / "src" / "00_bringup" / "001_hpu_smoke"
                / "06_dload_dstore_poll_mmio.c")

MOCK_API = r"""
#ifndef MOCK_DMA_POLL_API_H
#define MOCK_DMA_POLL_API_H
#include <stdint.h>
#include <stdio.h>
#define CSR_BASE_LO 0x08000000U
#define CSR_BASE_HI 0x08000004U
#define CSR_SIZE_LO 0x08000008U
#define CSR_SIZE_HI 0x0800000cU
#define CSR_COMMIT  0x08000010U
#define CSR_STATUS 0x08000014U
#define CSR_FAULT  0x08000018U
#define CSR_IRQ    0x0800001cU
#define STATUS_VALID 1U
#define STATUS_BUSY  2U
#define STATUS_FAULT 4U
#define FAULT_VALID 1U
#define IRQ_LEVEL 1U
#define COMMIT 1U
#define MEM_BASE UINT64_C(0x80000000)
#define SMOKE_LINES 256U
#define LINE_A 1U
#define LINE_OUT 129U
#define RNS_LINES 64U
#define P0 0U
#define RNS_A ((const uint32_t *)(uintptr_t)1U)
#define TIMEOUT 12U
uint32_t csr_read(uintptr_t address);
void csr_write(uintptr_t address, uint32_t value);
void case_start(const char *file);
int case_fail(const char *file, unsigned line);
int case_pass(const char *file);
int fixture_validate(void);
void fixture_copy(unsigned line, const uint32_t *data);
void fixture_poison(void);
int check_loopback(void);
int dload(unsigned object, unsigned line, unsigned length);
int dstore(unsigned object, unsigned line, unsigned length);
#endif
"""

HARNESS = r"""
#include <assert.h>
#include <stdlib.h>
#include "mock_dma_poll_api.h"

/* 直接包含真正的 06 main，避免只验证另一份轮询实现。 */
#define main smoke_main
#include SMOKE_SOURCE
#undef main

enum {
    DELAYED_SUCCESS = 1,
    LOAD_BUSY_FOREVER, STORE_BUSY_FOREVER,
    LOAD_NEVER_BUSY, STORE_NEVER_BUSY,
    LOAD_STATUS_FAULT, STORE_STATUS_FAULT,
    LOAD_DETAIL_FAULT, STORE_DETAIL_FAULT,
    LOAD_INVALID_WINDOW, STORE_INVALID_WINDOW,
    COMPARE_FAILURE,
    LOAD_ISSUE_FAILURE, STORE_ISSUE_FAILURE
};

static unsigned scenario, phase, checked, failed, passed, copied, poisoned;
static unsigned polls[3], observed_busy[3], observed_done[3];
static unsigned fault_reads[3], loads, stores;
static uint32_t config[4];
static int committed;

static int phase_scenario(unsigned load_case, unsigned store_case) {
    return (phase == 1U && scenario == load_case) ||
           (phase == 2U && scenario == store_case);
}

uint32_t csr_read(uintptr_t address) {
    /* CSR_IRQ 即使被重新引入，也绝不能成为 06 的同步依赖。 */
    assert(address != CSR_IRQ);
    if (address < CSR_COMMIT) {
        assert(address >= CSR_BASE_LO && address % sizeof(uint32_t) == 0U);
        return config[(address - CSR_BASE_LO) / sizeof(uint32_t)];
    }
    if (address == CSR_FAULT) {
        ++fault_reads[phase];
        if (polls[phase] >= 4U &&
            phase_scenario(LOAD_DETAIL_FAULT, STORE_DETAIL_FAULT))
            return FAULT_VALID;
        return 0U;
    }
    assert(address == CSR_STATUS && committed);
    if (phase == 0U) return STATUS_VALID;

    unsigned count = ++polls[phase];
    unsigned first_busy = phase == 1U ? 3U : 4U;
    uint32_t status = STATUS_VALID;
    if (!phase_scenario(LOAD_NEVER_BUSY, STORE_NEVER_BUSY) &&
        count >= first_busy &&
        (count < first_busy + 2U ||
         phase_scenario(LOAD_BUSY_FOREVER, STORE_BUSY_FOREVER)))
        status |= STATUS_BUSY;
    if (count >= 4U &&
        phase_scenario(LOAD_STATUS_FAULT, STORE_STATUS_FAULT))
        status |= STATUS_FAULT;
    if (count >= 4U &&
        phase_scenario(LOAD_INVALID_WINDOW, STORE_INVALID_WINDOW))
        status &= ~STATUS_VALID;

    if ((status & STATUS_BUSY) != 0U) observed_busy[phase] = 1U;
    else if (observed_busy[phase] != 0U) observed_done[phase] = 1U;
    return status;
}

void csr_write(uintptr_t address, uint32_t value) {
    assert(address != CSR_IRQ);
    if (address < CSR_COMMIT) {
        assert(address >= CSR_BASE_LO && address % sizeof(uint32_t) == 0U);
        config[(address - CSR_BASE_LO) / sizeof(uint32_t)] = value;
    } else if (address == CSR_COMMIT) {
        assert(value == COMMIT);
        committed = 1;
    } else {
        assert(address == CSR_FAULT && value == FAULT_VALID);
    }
}

void case_start(const char *file) { (void)file; }
int case_fail(const char *file, unsigned line) {
    (void)file;
    (void)line;
    ++failed;
    return 1;
}
int case_pass(const char *file) { (void)file; ++passed; return 0; }
int fixture_validate(void) { return 0; }
void fixture_copy(unsigned line, const uint32_t *data) {
    assert(phase == 0U && line == LINE_A && data == RNS_A);
    ++copied;
}
void fixture_poison(void) {
    assert(phase == 0U);
    ++poisoned;
}

int dload(unsigned object, unsigned line, unsigned length) {
    assert(phase == 0U && copied == 1U && poisoned == 1U);
    assert(object == P0 && line == LINE_A && length == RNS_LINES);
    ++loads;
    phase = 1U;
    return scenario == LOAD_ISSUE_FAILURE;
}

int dstore(unsigned object, unsigned line, unsigned length) {
    /* 必须实际看到 DLOAD 的忙转闲，不能初始 idle 就发送 DSTORE。 */
    assert(phase == 1U && observed_busy[1] && observed_done[1]);
    assert(polls[1] == 5U);
    assert(object == P0 && line == LINE_OUT && length == RNS_LINES);
    ++stores;
    phase = 2U;
    return scenario == STORE_ISSUE_FAILURE;
}

int check_loopback(void) {
    /* DSTORE 必须单独等待：不能复用 DLOAD 的 saw_busy 提前自检。 */
    assert(phase == 2U && observed_busy[2] && observed_done[2]);
    assert(polls[2] == 6U);
    ++checked;
    return scenario == COMPARE_FAILURE;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = (unsigned)atoi(argv[1]);
    assert(scenario >= DELAYED_SUCCESS && scenario <= STORE_ISSUE_FAILURE);
    int result = smoke_main();
    assert(result == (scenario == DELAYED_SUCCESS ? 0 : 1));
    assert(passed == (unsigned)(scenario == DELAYED_SUCCESS));
    assert(failed == (unsigned)(scenario != DELAYED_SUCCESS));
    assert(loads == 1U && copied == 1U && poisoned == 1U);

    int load_failure = scenario == LOAD_BUSY_FOREVER ||
        scenario == LOAD_NEVER_BUSY || scenario == LOAD_STATUS_FAULT ||
        scenario == LOAD_DETAIL_FAULT || scenario == LOAD_INVALID_WINDOW ||
        scenario == LOAD_ISSUE_FAILURE;
    assert(stores == (unsigned)!load_failure);
    assert(checked == (unsigned)(scenario == DELAYED_SUCCESS ||
                                scenario == COMPARE_FAILURE));
    if (phase_scenario(LOAD_BUSY_FOREVER, STORE_BUSY_FOREVER) ||
        phase_scenario(LOAD_NEVER_BUSY, STORE_NEVER_BUSY))
        assert(polls[phase] == TIMEOUT);
    if (phase_scenario(LOAD_STATUS_FAULT, STORE_STATUS_FAULT) ||
        phase_scenario(LOAD_DETAIL_FAULT, STORE_DETAIL_FAULT) ||
        phase_scenario(LOAD_INVALID_WINDOW, STORE_INVALID_WINDOW))
        assert(polls[phase] == 4U);
    if (phase_scenario(LOAD_ISSUE_FAILURE, STORE_ISSUE_FAILURE))
        assert(polls[phase] == 0U);
    if (scenario == DELAYED_SUCCESS || scenario == COMPARE_FAILURE) {
        /* 正常等待的每一轮都必须查看独立 FAULT，不能只看汇总位。 */
        assert(fault_reads[1] == polls[1]);
        assert(fault_reads[2] == polls[2]);
    }
    return 0;
}
"""


class DmaPollTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = TEST_ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="dma-poll-host-", dir=build)
        cls.addClassCleanup(cls.tmp.cleanup)
        root = Path(cls.tmp.name)
        (root / "hpu").mkdir()
        (root / "mock_dma_poll_api.h").write_text(MOCK_API, encoding="utf-8")
        for header in ("result", "csr", "dma", "fixture", "layout"):
            (root / "hpu" / f"{header}.h").write_text(
                '#include "mock_dma_poll_api.h"\n', encoding="utf-8")
        for header in ("sync", "irq", "completion"):
            (root / "hpu" / f"{header}.h").write_text(
                '#error "06 must not depend on PSYNC or IRQ helpers"\n',
                encoding="utf-8")
        source = root / "test.c"
        source.write_text(HARNESS, encoding="utf-8")
        cls.binary = root / "test"
        subprocess.run(
            shlex.split(os.environ.get("HOST_CC", "cc"))
            + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f'-DSMOKE_SOURCE="{SMOKE_SOURCE}"', "-I", str(root),
               str(source), "-o", str(cls.binary)],
            check=True,
        )

    def test_no_psync_or_irq_dependency(self):
        # 去除注释后检查标识符；中文说明可以解释为何不使用 PSYNC。
        source = re.sub(r"/\*.*?\*/|//[^\n]*", "", SMOKE_SOURCE.read_text(),
                        flags=re.DOTALL)
        self.assertNotRegex(source, r"\b(?:psync|irq_open|CSR_IRQ)\b")

    def test_actual_case_status_polling(self):
        for scenario in range(1, 15):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.binary), str(scenario)], check=True)


if __name__ == "__main__":
    unittest.main()
