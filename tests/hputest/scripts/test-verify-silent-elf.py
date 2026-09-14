#!/usr/bin/env python3
"""小型 ELF 头和工具输出 fixture 验证静默检查器；不执行 RISC-V 或 VCS。"""

import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "verify_silent_elf", Path(__file__).with_name("verify-silent-elf.py"))
VERIFY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VERIFY
SPEC.loader.exec_module(VERIFY)

NM = """0000000080000100 0000000000000004 T main
0000000080000110 0000000000000006 T _halt
0000000080000120 000000000000000c T __wrap_printf_
0000000080000140 0000000000000002 T _putc
"""
DISASSEMBLY = """
fixture.elf:     file format elf64-littleriscv

Disassembly of section .text:
0000000080000100 <main>:
    80000100: 4501                 li a0,0
    80000102: 8082                 ret
0000000080000110 <_halt>:
    80000110: 0005006b             .word 0x0005006b
    80000114: a001                 j 80000114 <_halt+0x4>
0000000080000120 <__wrap_printf_>:
    80000120: 7139                 add sp,sp,-64
    80000122: e42e                 sd a1,8(sp)
    80000124: e832                 sd a2,16(sp)
    80000126: 4501                 li a0,0
    80000128: 6121                 add sp,sp,64
    8000012a: 8082                 ret
0000000080000140 <_putc>:
    80000140: b7c5                 j 80000120 <__wrap_printf_>
"""


class SilentElfTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="silent-elf-unit-")
        self.addCleanup(self.temporary.cleanup)
        self.elf = Path(self.temporary.name) / "fixture.elf"
        self.header = bytearray(64)
        self.header[:7] = b"\x7fELF\x02\x01\x01"
        struct.pack_into("<HHIQ", self.header, 16, 2, 243, 1, 0x80000000)
        struct.pack_into("<H", self.header, 52, 64)
        self.nm = NM
        self.disassembly = DISASSEMBLY
        self.calls = []

    def tool(self, arguments, **kwargs):
        self.calls.append(arguments)
        self.assertTrue(kwargs["check"])
        self.assertTrue(kwargs["text"])
        self.assertEqual(arguments[-1], str(self.elf))
        if arguments[0] == "fixture-nm":
            self.assertEqual(arguments[1:-1], ["-S", "--defined-only"])
            return subprocess.CompletedProcess(arguments, 0, self.nm, "")
        self.assertEqual(arguments[0], "fixture-objdump")
        self.assertEqual(arguments[1:-1], ["-d"])
        return subprocess.CompletedProcess(arguments, 0, self.disassembly, "")

    def verify(self):
        self.elf.write_bytes(self.header)
        with patch.object(VERIFY.subprocess, "run", side_effect=self.tool):
            VERIFY.verify(self.elf, "fixture-")

    def test_inert_varargs_stack_and_bridge(self):
        self.verify()
        self.assertEqual(len(self.calls), 2)

    def test_gc_can_remove_all_wrappers_and_bridges(self):
        self.nm = "\n".join(NM.splitlines()[:2])
        self.disassembly = DISASSEMBLY.split("0000000080000120")[0]
        self.verify()

    def test_missing_or_wrong_elf_header(self):
        for offset, value in ((0, 0), (4, 1), (5, 2), (6, 0), (16, 3), (18, 62), (52, 0)):
            with self.subTest(offset=offset):
                original = self.header[offset]
                self.header[offset] = value
                with self.assertRaisesRegex(ValueError, "ELF"):
                    self.verify()
                self.header[offset] = original
        self.header = self.header[:20]
        with self.assertRaisesRegex(ValueError, "ELF"):
            self.verify()

    def test_wrong_entry(self):
        struct.pack_into("<Q", self.header, 24, 0x80000004)
        with self.assertRaisesRegex(ValueError, "entry"):
            self.verify()

    def test_required_main_and_halt(self):
        for name in ("main", "_halt"):
            with self.subTest(name=name):
                self.nm = "\n".join(line for line in NM.splitlines()
                                    if not line.endswith(" " + name))
                with self.assertRaisesRegex(ValueError, "missing sized function"):
                    self.verify()

    def test_stop_word_must_be_inside_halt(self):
        self.disassembly = DISASSEMBLY.replace("0005006b", "00000013")
        with self.assertRaisesRegex(ValueError, "stop instruction"):
            self.verify()

    def test_real_output_symbols_rejected_even_if_uncalled(self):
        for name in sorted(VERIFY.FORBIDDEN):
            with self.subTest(name=name):
                self.nm = NM + f"0000000080000200 0000000000000004 T {name}\n"
                with self.assertRaisesRegex(ValueError, "real formatter/UART"):
                    self.verify()

    def test_wrapper_rejects_mmio_csr_call_and_loop(self):
        for instruction in ("sw a1,0(a0)", "ld a0,0(a1)", "csrr a0,mstatus",
                            "j 80000126 <__wrap_printf_+0x6>",
                            "jal 80000100 <main>", "ebreak"):
            with self.subTest(instruction=instruction):
                self.disassembly = DISASSEMBLY.replace("li a0,0\n    80000128", instruction + "\n    80000128")
                with self.assertRaisesRegex(ValueError, "non-inert"):
                    self.verify()

    def test_wrapper_stack_must_be_bounded_and_balanced(self):
        for old, new in (("sd a1,8(sp)", "sd a1,64(sp)"),
                         ("add sp,sp,64", "add sp,sp,48"),
                         ("add sp,sp,-64", "add sp,sp,-8192")):
            with self.subTest(new=new):
                self.disassembly = DISASSEMBLY.replace(old, new)
                with self.assertRaisesRegex(ValueError, "stack"):
                    self.verify()

    def test_unknown_decode_cannot_bypass_wrapper_validation(self):
        self.disassembly = DISASSEMBLY.replace("    80000126: 4501                 li a0,0\n", "")
        with self.assertRaisesRegex(ValueError, "undecoded"):
            self.verify()

    def test_bridge_cannot_call_real_code_or_do_mmio(self):
        for instruction in ("j 80000100 <main>", "sd a0,0(a1)",
                            "j 80000122 <__wrap_printf_>"):
            with self.subTest(instruction=instruction):
                self.disassembly = DISASSEMBLY.replace("j 80000120 <__wrap_printf_>", instruction)
                with self.assertRaisesRegex(ValueError, "non-inert"):
                    self.verify()

    def test_malformed_nm_or_function_boundary(self):
        self.nm = NM + "unrecognized row\n"
        with self.assertRaisesRegex(ValueError, "nm row"):
            self.verify()
        self.nm = NM.replace("000000000000000c T __wrap", "000000000000000b T __wrap")
        with self.assertRaisesRegex(ValueError, "boundary"):
            self.verify()

    def test_tool_failure_is_not_success(self):
        self.elf.write_bytes(self.header)
        with patch.object(VERIFY.subprocess, "run", side_effect=FileNotFoundError("no nm")):
            with self.assertRaisesRegex(ValueError, "tool failed"):
                VERIFY.verify(self.elf, "fixture-")


if __name__ == "__main__":
    unittest.main()
