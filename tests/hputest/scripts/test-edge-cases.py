#!/usr/bin/env python3
"""边缘用例的CPU参考、软件等待与实际ELF检查；不替代RTL/VCS或队列monitor。"""
import argparse
import csv
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
with (ROOT / "edge-cases.tsv").open() as stream:
    CASES = list(csv.DictReader(stream, delimiter="\t"))


def cpu_step(value):
    value ^= (value << 13) & 0xffffffff
    value ^= value >> 17
    value ^= (value << 5) & 0xffffffff
    return value & 0xffffffff


class EdgeTests(unittest.TestCase):
    def compile_run(self, source, include=None, extra=None):
        cache = ROOT / "build"
        cache.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="edge-test-", dir=cache) as temporary:
            directory = Path(temporary)
            if include:
                for name, data in include.items():
                    path = directory / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(data)
            path = directory / "test.c"
            path.write_text(source)
            binary = directory / "test"
            command = [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(directory), "-I" + str(ROOT / "include"), str(path)]
            subprocess.run(command + (extra or []) + ["-o", str(binary)], check=True)
            return subprocess.check_output([str(binary)], text=True)

    def test_case_matrix_is_flat_and_watchdog_has_explicit_fault_contract(self):
        self.assertEqual(len(CASES), 10)
        self.assertEqual({int(c["commands"]) for c in CASES if c["kind"] == "contiguous-burst"}, {1, 8, 9, 32, 64})
        with (ROOT / "cases.tsv").open() as stream:
            roster = {c["case_id"]: c for c in csv.DictReader(stream, delimiter="\t")}
        for case in CASES:
            path = Path(case["source"])
            self.assertEqual(len(path.parts), 4)
            self.assertEqual(path.stem, case["case_id"])
            self.assertEqual(roster[case["case_id"]]["source"], case["source"])
            text = (ROOT / path).read_text()
            if case["kind"].startswith("watchdog-"):
                self.assertEqual(roster[case["case_id"]]["qualifier"], "software-self-check")
                self.assertIn("fault_code(fault) != WD_CODE", text)
                if case["kind"] == "watchdog-fail-stop":
                    self.assertIn("watchdog_fault_match", text)
                    self.assertIn("check_window", text)
                    self.assertNotIn("csr_write(CSR_COMMIT", text)
                else:
                    self.assertIn("wd_fault_expected(status, fault, 1U)", text)
                    self.assertIn("wd_check_memory", text)
                    self.assertIn("csr_write(CSR_COMMIT, COMMIT)", text)
                    self.assertIn("wait_window(1)", text)
                self.assertNotIn("irq_open", text)
                self.assertEqual(text.count("    psync();"), 1)
            else:
                self.assertEqual(int(case["words"]), 64)
                self.assertEqual(text.count("    psync();"), 1)
                self.assertIn("v2_check_memory", text)
                self.assertIn("edge_wait(EDGE_WAIT_CYCLES)", text)

    def test_watchdog_fault_contract_rejects_other_faults_and_cdc_skew(self):
        mock = '#include <stdint.h>\n#define STATUS_VALID 1U\n#define STATUS_BUSY 2U\n#define STATUS_FAULT 4U\n#define FAULT_VALID 1U\n'
        self.compile_run(r'''
#include <assert.h>
#include <hpu/watchdog.h>
int main(void) {
    assert(WD_CYCLES == 500000U);
    assert(watchdog_fault_match(4U, 0x101U));
    assert(!watchdog_fault_match(0U, 0x101U));
    assert(!watchdog_fault_match(4U, 0U));
    assert(!watchdog_fault_match(4U, 1U));
    assert(!watchdog_fault_match(4U, 0x201U));
    assert(!watchdog_fault_match(5U, 0x101U));
    assert(!watchdog_fault_match(6U, 0x101U));
    assert(fault_code(0xff01U) == 255U);
    assert(wd_fault_expected(5U, 0x101U, 1U));
    assert(wd_fault_expected(4U, 0x101U, 0U));
    assert(!wd_fault_expected(4U, 0x101U, 1U));
    assert(!wd_fault_expected(5U, 0x101U, 0U));
    assert(!wd_fault_expected(7U, 0x101U, 1U));
    assert(!wd_fault_expected(5U, 1U, 1U));
    for (unsigned bit = 0; bit < 16; ++bit)
        if (WD_FAULT_MASK & (1U << bit))
            assert(!watchdog_fault_match(4U, 0x101U ^ (1U << bit)));
    return 0;
}
''', {"hpu/csr.h": mock})

    def test_pure_c_reference_against_independent_python(self):
        output = self.compile_run(r'''
#include <hpu/edge_reference.h>
#include <stdio.h>
int main(void) {
    const unsigned counts[] = {1,8,9,32,64};
    const uint32_t q = 50061313U;
    for (unsigned i = 0; i < 5; ++i)
        printf("B %u %u\n", counts[i], edge_reference_burst(q-2U,q-1U,counts[i],q));
    uint32_t state = 0x08120001U, scratch[8] = {0U}, choice;
    for (unsigned i = 0; i < 16; ++i) {
        state = edge_reference_memory(state,i,scratch,&choice);
        printf("M %u %u %u\n",i,state,choice);
    }
    for (unsigned i = 0; i < 8; ++i) printf("S %u %u\n",i,scratch[i]);
    return 0;
}
''')
        lines = output.splitlines()
        q = 50061313
        for index, n in enumerate((1, 8, 9, 32, 64)):
            value = q - 2
            for command in range(n):
                value = (value * 7) % q if command & 1 else (value + q - 1) % q
            self.assertEqual(lines[index], f"B {n} {value}")
        state = 0x08120001
        scratch = [0] * 8
        choices = []
        for i in range(16):
            state = cpu_step(state ^ i)
            index = (state >> 3) & 7
            loaded = state ^ ((0x9e3779b9 * (i + 1)) & 0xffffffff)
            scratch[index] = loaded
            choice = loaded & 1
            state = loaded ^ scratch[(index + 1) & 7]
            choices.append(choice)
            self.assertEqual(lines[5+i], f"M {i} {state} {choice}")
        self.assertEqual(set(choices), {0, 1})
        self.assertEqual(lines[21:], [f"S {i} {value}" for i, value in enumerate(scratch)])

    def test_actual_wait_rejects_busy_missing_event_fault_and_budget(self):
        mock = r'''
#include <stdint.h>
#include <assert.h>
#define CSR_IRQ 1U
#define CSR_STATUS 2U
#define CSR_FAULT 3U
#define IRQ_LEVEL 1U
#define STATUS_VALID 1U
#define STATUS_BUSY 2U
#define STATUS_FAULT 4U
#define FAULT_VALID 1U
struct trace { uint32_t irq,status,fault; uint64_t wait_cycles; };
extern volatile struct trace application_trace;
static inline uint32_t csr_read(uintptr_t address) {(void)address; return 0;}
static inline int case_fail(const char *file,unsigned line) {(void)file;(void)line;return 1;}
int edge_wait(uint64_t budget);
'''
        harness = r'''
#include <hpu/edge.h>
volatile struct trace application_trace;
static uint64_t clock_value;
static unsigned scenario,rounds;
uint64_t edge_host_cycle(void) { return clock_value++; }
uint32_t edge_host_read(uintptr_t address) {
    if (address == CSR_IRQ) { ++rounds; return scenario == 2 ? 0U : IRQ_LEVEL; }
    if (address == CSR_FAULT) return scenario == 5 ? FAULT_VALID : 0U;
    uint32_t status = scenario == 3 ? 0U : STATUS_VALID;
    if (scenario == 1 || (scenario == 0 && rounds < 4)) status |= STATUS_BUSY;
    if (scenario == 4) status |= STATUS_FAULT;
    return status;
}
int main(void) {
    scenario=0;rounds=0;clock_value=0;assert(edge_wait(8)==0);assert(rounds==4);
    for (scenario=1;scenario<=5;++scenario) {rounds=0;clock_value=0;assert(edge_wait(8)==1);assert(rounds<=8);}
    scenario=0;rounds=0;clock_value=0;assert(edge_wait(0)==1);assert(rounds==0);
    scenario=1;rounds=0;clock_value=UINT64_MAX-4U;assert(edge_wait(8)==1);assert(rounds==8);
    return 0;
}
'''
        log = '#include <stdio.h>\n#define LOG_ERROR(...) do { if (0) printf(__VA_ARGS__); } while (0)\n'
        self.compile_run(harness, {"hpu/edge.h": mock, "hpu/log.h": log},
                         ["-DHPU_EDGE_HOST_TEST", "-DHPU_LOG_LEVEL=0", str(ROOT / "runtime/it_edge.c")])

    @unittest.skipUnless(os.environ.get("HPU_EDGE_ELF_ROOT"), "ELF验收由实际交叉编译产物提供")
    def test_actual_elf_burst_counts_order_and_cpu_mix(self):
        directory = Path(os.environ["HPU_EDGE_ELF_ROOT"])
        generated = Path(os.environ["HPU_GENERATED_ROOT"])
        header = (generated / "include/hpu/inline_asm_mm_delivery.h").read_text()
        def encoded(name):
            match = re.search(r"#define\s+" + name + r"\s+(?:UINT32_C\()?\s*(0x[0-9a-fA-F]+)", header)
            self.assertIsNotNone(match, name)
            return int(match[1], 16)
        add, mul = encoded("HPU_INSN_PADD_P0_P0_P1"), encoded("HPU_INSN_PMUL_IMM7_P0_P0")
        checked = 0
        for case in CASES:
            elf = directory / (case["case_id"] + ".elf")
            if not elf.is_file(): continue  # make one只验收实际选择项。
            checked += 1
            symbols = subprocess.check_output(["riscv64-linux-gnu-nm", str(elf)], text=True)
            if case["kind"] == "watchdog-fail-stop":
                for prefix, name in (("watchdog_stall", "HPU_INSN_DLOAD_P0_POLY"),
                                     ("watchdog_drop", "HPU_INSN_DLOAD_P1_POLY")):
                    first = int(re.search(r"^([0-9a-f]+)\s+\w\s+" + prefix + r"_begin$", symbols, re.M)[1], 16)
                    last = int(re.search(r"^([0-9a-f]+)\s+\w\s+" + prefix + r"_end$", symbols, re.M)[1], 16)
                    asm = subprocess.check_output(["riscv64-linux-gnu-objdump", "-d", "--start-address="+str(first), "--stop-address="+str(last), str(elf)], text=True)
                    words = [int(w,16) for w in re.findall(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s", asm, re.M)]
                    self.assertEqual(last-first, 64*4)
                    self.assertEqual(words, [encoded(name)] * 64)
                continue
            if case["kind"].startswith("watchdog-"):
                first = int(re.search(r"^([0-9a-f]+)\s+\w\s+watchdog_order_begin$", symbols, re.M)[1], 16)
                last = int(re.search(r"^([0-9a-f]+)\s+\w\s+watchdog_order_end$", symbols, re.M)[1], 16)
                asm = subprocess.check_output(["riscv64-linux-gnu-objdump", "-d", "--start-address="+str(first), "--stop-address="+str(last), str(elf)], text=True)
                words = [int(w,16) for w in re.findall(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s", asm, re.M)]
                # 忽略offset递增的普通CPU指令，只验收真实HPU命令的编码与顺序。
                words = [w for w in words if (w & 127) in (0x2b, 0x5b)]
                mod = encoded("HPU_INSN_PMODLD_0")
                loadmod = encoded("HPU_INSN_DLOAD_P4_MOD")
                if case["kind"] == "watchdog-compute-before-dload":
                    expect = [add, encoded("HPU_INSN_DLOAD_P0_POLY"),
                              encoded("HPU_INSN_DLOAD_P1_POLY"), loadmod, mod] + [add]*64
                else:
                    expect = [mod, loadmod] + [mod]*64
                self.assertEqual(len(words), int(case["commands"]))
                self.assertEqual(words, expect)
                continue
            prefix = "edge_burst" if case["kind"] == "contiguous-burst" else "edge_mix"
            first = int(re.search(r"^([0-9a-f]+)\s+\w\s+" + prefix + r"_begin$", symbols, re.M)[1],16)
            last = int(re.search(r"^([0-9a-f]+)\s+\w\s+" + prefix + r"_end$", symbols, re.M)[1],16)
            disassembly = subprocess.check_output(["riscv64-linux-gnu-objdump", "-d", "--start-address="+str(first), "--stop-address="+str(last), str(elf)], text=True)
            if case["kind"] == "contiguous-burst":
                words = [int(w,16) for w in re.findall(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s",disassembly,re.M)]
                n = int(case["commands"])
                self.assertEqual(last-first, 4*n)
                self.assertEqual(words, [add,mul]*(n//2)+([add] if n&1 else []))
            else:
                self.assertRegex(disassembly, r"\bsllw\b|\bslliw\b")
                self.assertRegex(disassembly, r"\bsrlw\b|\bsrliw\b")
                self.assertRegex(disassembly, r"\bxor\b")
                self.assertRegex(disassembly, r"\bsw\b")
                if case["kind"] == "mixed-memory-branch":
                    self.assertRegex(disassembly,r"\blw\b|\blwu\b")
                    self.assertRegex(disassembly,r"\bbnez\b|\bbeqz\b|\bbne\b|\bbeq\b")
        self.assertGreater(checked, 0, "edge ELF selection is empty")


if __name__ == "__main__":
    unittest.main()
