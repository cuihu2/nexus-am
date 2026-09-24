#!/usr/bin/env python3
"""编译实际平台/PLIC/HPU中断代码，用主机MMIO桩验证地址及两轮中断。

仅替换MMIO原语和RISC-V内联汇编，不模拟RTL、PLIC硬件或真实中断时序。
"""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
AM = ROOT.parents[1]

AM_HEADER = r"""
#ifndef MOCK_AM_H
#define MOCK_AM_H
#include <stdint.h>
typedef struct { int unused; } _Context;
typedef struct { int event; } _Event;
enum { _EVENT_IRQ_IODEV = 1 };
void _intr_write(int enable);
int _cte_init(void *unused);
#endif
"""
CSR_HEADER = r"""
#include <stdint.h>
#define CSR_STATUS 0x08000014U
#define CSR_FAULT 0x08000018U
#define CSR_IRQ 0x0800001cU
#define STATUS_VALID 1U
#define STATUS_BUSY 2U
#define STATUS_FAULT 4U
#define FAULT_VALID 1U
#define IRQ_LEVEL 1U
#define TIMEOUT 32U
uint32_t csr_read(uintptr_t address);
void csr_write(uintptr_t address, uint32_t value);
"""
HARNESS = r"""
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
struct cell { uintptr_t address; uint32_t value; };
static struct cell cells[512];
static unsigned used, plic_accesses, claims, completions, init_calls;
static int enabled, bad_readback;
static uint32_t hpu_level;
int g_config_disable_timer;
static struct cell *cell(uintptr_t address) {
    /* 旧0x04窗口会立即触发断言，不可能靠默认读回0蒙混通过。 */
    assert(address >= 0x3c000000UL && address <= 0x3c201004UL);
    for (unsigned i = 0; i < used; ++i) if (cells[i].address == address) return &cells[i];
    assert(used < 512U);
    cells[used].address = address;
    return &cells[used++];
}
static uint32_t host_read(uintptr_t address) {
    ++plic_accesses;
    if (address == 0x3c201004UL) ++claims;
    if (bad_readback && address == 0x3c000404UL) return 0U;
    return cell(address)->value;
}
static void host_write(uintptr_t address, uint32_t value) {
    ++plic_accesses;
    if (address == 0x3c201004UL) {
        assert(value == 257U);
        assert(hpu_level == 0U); /* 先清HPU电平，再complete PLIC。 */
        ++completions;
    }
    cell(address)->value = value;
}

/* 这里插入实际的 plic.c、hpu_irq.c，仅把MMIO和ISA原语换成上述桩。 */
@PRODUCTION@

static _Context *(*registered)(_Event, _Context *);
void seip_handler_reg(_Context *(*function)(_Event, _Context *)) { registered = function; }
void _intr_write(int value) { enabled = value; }
int _cte_init(void *unused) {
    (void)unused;
    assert(g_config_disable_timer == 1);
    ++init_calls;
    plic_init(257U, 2U);
    for (unsigned source = 1U; source <= 257U; ++source)
        assert(cell(0x3c000000UL + 4U * source)->value == 0U);
    for (unsigned context = 0U; context < 2U; ++context) {
        for (unsigned word = 0U; word < 9U; ++word)
            assert(cell(0x3c002000UL + 0x80U * context + 4U * word)->value == 0U);
        assert(cell(0x3c200000UL + 0x1000U * context)->value == 7U);
    }
    return 0;
}
uint32_t csr_read(uintptr_t address) {
    if (address == CSR_STATUS) return STATUS_VALID;
    if (address == CSR_FAULT) return 0U;
    assert(address == CSR_IRQ);
    return hpu_level;
}
void csr_write(uintptr_t address, uint32_t value) {
    assert(address == CSR_IRQ);
    assert(value == IRQ_LEVEL || value == 0U);
    if (value == IRQ_LEVEL) hpu_level = 0U;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    _Static_assert(PLIC_PRIORITY_ADDR == 0x3c000404UL, "priority source257/index256");
    _Static_assert(PLIC_ENABLE_ADDR == 0x3c0020a0UL, "enable source257/context1");
    _Static_assert(PLIC_THRESHOLD_ADDR == 0x3c201000UL, "threshold context1");
    _Static_assert(PLIC_CLAIM_ADDR == 0x3c201004UL, "claim context1");
    if (atoi(argv[1]) == 2) {
        bad_readback = 1;
        assert(irq_open() == 1 && enabled == 0);
        assert(claims == 0 && completions == 0);
        return 0;
    }
    assert(irq_open() == 0 && enabled == 1 && init_calls == 1U);
    assert(cell(0x3c000404UL)->value == 1U);
    assert(cell(0x3c0020a0UL)->value == 2U);
    assert(cell(0x3c201000UL)->value == 0U);
    cell(0x3c0020a0UL)->value |= 8U; /* close必须保留其它源enable位。 */
    for (unsigned round = 0U; round < 2U; ++round) {
        _Context context = {0};
        cell(0x3c201004UL)->value = 257U;
        hpu_level = IRQ_LEVEL;
        assert(registered((_Event){_EVENT_IRQ_IODEV}, &context) == &context);
        assert(irq_wait() == 0 && hpu_level == 0U);
        assert(claims == round + 1U && completions == round + 1U);
        if (round == 0U) {
            unsigned before = plic_accesses;
            assert(irq_rearm() == 0 && enabled == 1);
            assert(plic_accesses == before); /* 重用原配置，不重新claim/初始化。 */
            assert(cell(0x3c000404UL)->value == 1U);
            assert(cell(0x3c0020a0UL)->value == 10U);
        }
    }
    irq_close();
    assert(enabled == 0 && cell(0x3c000404UL)->value == 0U);
    assert(cell(0x3c0020a0UL)->value == 8U);
    return 0;
}
"""


class PlicAddressTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="plic-address-", dir=build)
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        (cls.directory / "hpu").mkdir()
        headers = {"am.h": AM_HEADER, "klib.h": "#include <stdio.h>\n",
                   "isa_mock.h": "", "hpu/csr.h": CSR_HEADER,
                   "hpu/irq.h": "", "hpu/layout.h": "",
                   "xsextra.h": '#include <am.h>\nvoid seip_handler_reg(_Context *(*handler)(_Event, _Context *));\n'}
        for name, contents in headers.items():
            (cls.directory / name).write_text(contents, encoding="utf-8")
        cls.compiler = shlex.split(os.environ.get("HOST_CC", "cc")) + [
            "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(cls.directory), "-I", str(AM / "am/src/xs/include"),
            "-I", str(ROOT / "include"), '-DISA_H="isa_mock.h"']
        plic = (AM / "am/src/xs/isa/riscv/plic.c").read_text()
        plic = re.sub(r"^#define READ_WORD\(addr\).*$", "#define READ_WORD(addr) host_read(addr)", plic, flags=re.M)
        plic = re.sub(r"^#define WRITE_WORD\(addr, data\).*$", "#define WRITE_WORD(addr, data) host_write(addr, data)", plic, flags=re.M)
        irq = (ROOT / "src/common/hpu_irq.c").read_text()
        irq, reads = re.subn(r"static uint32_t mmio_read32\([^)]*\) \{.*?\n\}",
                            "static uint32_t mmio_read32(uint64_t address) { return host_read(address); }", irq, flags=re.S)
        irq, writes = re.subn(r"static void mmio_write32\([^)]*\) \{.*?\n\}",
                             "static void mmio_write32(uint64_t address, uint32_t data) { host_write(address, data); }", irq, flags=re.S)
        irq, asm = re.subn(r'__asm__ volatile\([^;]*\);', '(void)0;', irq)
        assert (reads, writes, asm) == (1, 1, 5), "hardware seam changed; review test adapter"
        source = cls.directory / "irq_test.c"
        source.write_text(HARNESS.replace("@PRODUCTION@", plic + "\n" + irq), encoding="utf-8")
        cls.binaries = []
        for level in (0, 1, 2):
            binary = cls.directory / f"irq-level{level}"
            subprocess.run(cls.compiler + ["-D__ARCH_RISCV64_XS", "-DLINKNAN_HPU_IT=1",
                                           f"-DHPU_LOG_LEVEL={level}", str(source), "-o", str(binary)], check=True)
            cls.binaries.append(binary)

    def test_init_open_two_interrupts_rearm_and_close_use_platform_base(self):
        for binary in self.binaries:
            subprocess.run([str(binary), "1"], check=True, capture_output=True)

    def test_priority_readback_failure_does_not_claim_or_enable_interrupts(self):
        for binary in self.binaries:
            subprocess.run([str(binary), "2"], check=True, capture_output=True)

    def test_platform_header_variants(self):
        source = self.directory / "platform.c"
        source.write_text('#include <xs.h>\n_Static_assert(PLIC_BASE_ADDR == EXPECTED_BASE, "base");\n'
                          'int main(void) { return 0; }\n', encoding="utf-8")
        for arch, hpu, expected in (("__ARCH_RISCV64_XS", True, "0x3c000000UL"),
                                    ("__ARCH_RISCV64_XS", False, "0x3c000000UL"),
                                    ("__ARCH_RISCV64_XS_SOUTHLAKE", True, "0x1f1c000000UL")):
            subprocess.run(self.compiler + [f"-D{arch}", f"-DEXPECTED_BASE={expected}"] +
                           (["-DLINKNAN_HPU_IT=1"] if hpu else []) +
                           [str(source), "-o", str(self.directory / "platform")], check=True)


if __name__ == "__main__":
    unittest.main()
