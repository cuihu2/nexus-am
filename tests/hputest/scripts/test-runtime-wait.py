#!/usr/bin/env python3
"""提取并运行实际 wait_irq()，回归迁移用例的 MMIO 完成同步。

只替换 MMIO 读取和超时常量，不编译含 RISC-V 指令的其余 runtime。
这不是 HPU/PLIC 仿真，也不能代替 IT 环境中的功能验证。
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


TEST_ROOT = Path(__file__).resolve().parents[1]
MOCK_DECLARATIONS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#define HPU_CSR_STATUS_ADDR 0x08000014U
#define HPU_CSR_FAULT_ADDR  0x08000018U
#define HPU_CSR_IRQ_ADDR    0x0800001cU
#define HPU_STATUS_WINDOW_VALID 1U
#define HPU_STATUS_BUSY         2U
#define HPU_STATUS_FAULT_VALID  4U
#define HPU_FAULT_VALID         1U
#define HPU_IRQ_LEVEL           1U
#define HPU_TIMEOUT             8U
enum { STEP_OK = 0, STEP_ERR_FAULT = 204, STEP_ERR_TIMEOUT = 205 };

uint32_t hpu_csr_read32(uintptr_t address);
"""

HARNESS = r"""
static unsigned rounds, irq_at = 1U, idle_at;
static unsigned status_fault_at, fault_at;
static uint32_t valid = HPU_STATUS_WINDOW_VALID;

uint32_t hpu_csr_read32(uintptr_t address) {
    if (address == HPU_CSR_IRQ_ADDR) {
        ++rounds;
        assert(rounds <= HPU_TIMEOUT);
        return irq_at != 0U && rounds >= irq_at ? HPU_IRQ_LEVEL : 0U;
    }
    if (address == HPU_CSR_STATUS_ADDR) {
        uint32_t status = valid;
        if (idle_at == 0U || rounds < idle_at) status |= HPU_STATUS_BUSY;
        if (status_fault_at != 0U && rounds >= status_fault_at)
            status |= HPU_STATUS_FAULT_VALID;
        return status;
    }
    assert(address == HPU_CSR_FAULT_ADDR);
    return fault_at != 0U && rounds >= fault_at ? HPU_FAULT_VALID : 0U;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    switch (atoi(argv[1])) {
    case 1: /* IRQ 第一轮到达，BUSY 第四轮清零：必须等到第四轮才成功。 */
        idle_at = 4U;
        assert(wait_irq() == STEP_OK);
        assert(rounds == 4U);
        break;
    case 2: /* IRQ 已到但 BUSY 永远不清：不能提前失败，也不能成功。 */
        assert(wait_irq() == STEP_ERR_TIMEOUT);
        assert(rounds == HPU_TIMEOUT);
        break;
    case 3: /* 始终空闲但没有 IRQ：不能把空闲冒充完成事件。 */
        irq_at = 0U;
        idle_at = 1U;
        assert(wait_irq() == STEP_ERR_TIMEOUT);
        assert(rounds == HPU_TIMEOUT);
        break;
    case 4: /* 等待 BUSY 清零期间 STATUS 报故障，立即返回故障码。 */
        idle_at = 5U;
        status_fault_at = 3U;
        assert(wait_irq() == STEP_ERR_FAULT);
        assert(rounds == 3U);
        break;
    case 5: /* STATUS 无故障，独立 FAULT 寄存器第三轮报错也须发现。 */
        idle_at = 5U;
        fault_at = 3U;
        assert(wait_irq() == STEP_ERR_FAULT);
        assert(rounds == 3U);
        break;
    case 6: /* IRQ=1、BUSY=0，但窗口无效：仍不能通过。 */
        idle_at = 1U;
        valid = 0U;
        assert(wait_irq() == STEP_ERR_TIMEOUT);
        assert(rounds == HPU_TIMEOUT);
        break;
    default:
        abort();
    }
    return 0;
}
"""


def extract_wait_irq(source):
    """按花括号边界提取函数原文；注释和字符串中的括号不计数。"""
    masked = re.sub(
        r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda match: " " * len(match.group()),
        source,
    )
    matches = list(re.finditer(r"\bint\s+wait_irq\s*\(\s*void\s*\)\s*\{", masked))
    if len(matches) != 1:
        raise ValueError("it_core.c 必须恰好定义一个 int wait_irq(void) 函数")
    match = matches[0]
    depth = 1
    for end in range(match.end(), len(masked)):
        if masked[end] == "{":
            depth += 1
        elif masked[end] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():end + 1]
    raise ValueError("wait_irq() 的函数花括号未闭合")


class RuntimeWaitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        implementation = extract_wait_irq(
            (TEST_ROOT / "runtime" / "it_core.c").read_text(encoding="utf-8")
        )
        build = TEST_ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="runtime-wait-host-", dir=build)
        cls.addClassCleanup(cls.tmp.cleanup)
        root = Path(cls.tmp.name)
        source = root / "test.c"
        source.write_text(
            MOCK_DECLARATIONS + "\n" + implementation + "\n" + HARNESS,
            encoding="utf-8",
        )
        cls.binary = root / "test"
        subprocess.run(
            shlex.split(os.environ.get("HOST_CC", "cc"))
            + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               str(source), "-o", str(cls.binary)],
            check=True,
        )

    def test_runtime_completion_scenarios(self):
        for scenario in range(1, 7):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.binary), str(scenario)], check=True)


if __name__ == "__main__":
    unittest.main()
