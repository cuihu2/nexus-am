#!/usr/bin/env python3
"""将真实 C 单 stage golden 与固定 RTL dump、独立 DFT 和物理模型比较。"""

import csv
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

from hpu_ntt_layout import (expected_twiddles, forward_layout, physical_words,
                            stage_reference)

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "third_party/inline-asm/test/fixtures/ntt_hw_ut"


def column(path, name):
    with path.open(newline="", encoding="utf-8") as stream:
        return [int(row[name], 0) for row in csv.DictReader(stream)]


class StageReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="hpu-stage-host-")
        library = Path(cls.temp.name) / "stage.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        "-I", str(ROOT / "include"), str(ROOT / "runtime/it_stage_reference.c"),
                        "-o", str(library)], check=True)
        cls.library = ctypes.CDLL(str(library))
        cls.reference = cls.library.stage_golden
        pointer = ctypes.POINTER(ctypes.c_uint32)
        cls.reference.argtypes = [pointer, pointer, pointer] + [ctypes.c_uint] * 4
        cls.reference.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_c(self, data, twiddle, q, stage, inverse):
        source = (ctypes.c_uint32 * len(data))(*data)
        table = (ctypes.c_uint32 * len(twiddle))(*twiddle)
        output = (ctypes.c_uint32 * len(data))()
        self.assertEqual(self.reference(source, table, output, len(data), q, stage, inverse), 0)
        return list(output)

    def test_fixed_pntt_rtl_stage0(self):
        fixture = FIXTURES / "rtl_pntt"
        data = column(fixture / "obj0_poly.csv", "value")
        table = column(fixture / "obj1_twiddle.csv", "value")[:len(data) // 2]
        expected = column(fixture / "obj2_output_exp_vs_rtl.csv", "rtl_act")
        self.assertEqual(self.run_c(data, table, 0xFFFFFFFE, 0, 0), expected)

    def test_fixed_pintt_rtl_stage0_and_stage1(self):
        fixture = FIXTURES / "rtl_pintt"
        for stage, input_file, output_file in (
                (0, "obj0_polyA.csv", "obj2_stage0_output_exp_vs_rtl.csv"),
                (1, "obj1_polyB.csv", "obj3_stage1_output_exp_vs_rtl.csv")):
            with self.subTest(stage=stage):
                data = column(fixture / input_file, "value")
                table = ([1] * (len(data) // 2) if stage == 0 else
                         column(fixture / "obj4_twiddle.csv", "value")[:len(data) // 2])
                expected = column(fixture / output_file, "rtl_act")
                self.assertEqual(self.run_c(data, table, 0xFFFFFFFE, stage, 1), expected)

    def test_all_stages_and_loader_boundaries(self):
        q = 65537
        for n in (128, 512, 4096):
            data = [((i * 17) ^ (i >> 2)) % q for i in range(n)]
            twiddle = [(i * 37 + 11) % q for i in range(n // 2)]
            for inverse in (0, 1):
                for stage in range(n.bit_length() - 1):
                    with self.subTest(n=n, inverse=inverse, stage=stage):
                        self.assertEqual(self.run_c(data, twiddle, q, stage, inverse),
                                         stage_reference(data, twiddle, q, stage, bool(inverse)))

    def test_full_c_schedule_against_independent_direct_dft_and_roundtrip(self):
        n, q = 128, 65537
        psi = pow(3, (q - 1) // (2 * n), q)
        omega = psi * psi % q
        tables = expected_twiddles(n, q, psi)
        logical = [(i * i + 19) % q for i in range(n)]
        direct = [sum(value * pow(omega, i * k, q) for i, value in enumerate(logical)) % q
                  for k in range(n)]
        values = physical_words(logical)
        for stage, table in enumerate(tables["ntt"]):
            values = self.run_c(values, table, q, stage, 0)
        self.assertEqual(values, [direct[i] for i in forward_layout(n)])
        for stage, table in enumerate(tables["intt"]):
            values = self.run_c(values, table, q, stage, 1)
        values = [value * pow(n, -1, q) % q for value in values]
        self.assertEqual(values, physical_words(logical))

    def test_invalid_stage_and_alias_are_rejected(self):
        array = (ctypes.c_uint32 * 128)(*range(128))
        table = (ctypes.c_uint32 * 64)(*[1] * 64)
        out = (ctypes.c_uint32 * 128)()
        self.assertEqual(self.reference(array, table, out, 128, 65537, 7, 0), 1)
        self.assertEqual(self.reference(array, table, array, 128, 65537, 0, 0), 1)


if __name__ == "__main__":
    unittest.main()
