#!/usr/bin/env python3
"""用主机编译器运行实际 completion.h，验证 MMIO 完成事件的状态处理。

仅替换 MMIO 访问和超时常量；不模拟 RISC-V、PLIC 或实际 HPU 时序。
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
#define TIMEOUT 8U
uint32_t csr_read(uintptr_t address);
void csr_write(uintptr_t address, uint32_t value);
"""

HARNESS = r"""
#include <assert.h>
#include <stdlib.h>
#include <hpu/completion.h>

static uint32_t status = STATUS_VALID, fault, level;
static unsigned reads, done_at, clear_at, writes;
static int clearing, stay_busy, late_fault;
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
        if (!stay_busy) status &= ~STATUS_BUSY;
        if (late_fault) fault = FAULT_VALID;
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
    case 5: /* IRQ 到达时仍忙，不能通过。 */
        status |= STATUS_BUSY;
        stay_busy = 1;
        done_at = 1U;
        assert(completion_wait() == 1);
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
    default:
        abort();
    }
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

    def test_mmio_completion_scenarios(self):
        for scenario in range(1, 13):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.binary), str(scenario)], check=True)


if __name__ == "__main__":
    unittest.main()
