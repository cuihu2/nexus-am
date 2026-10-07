#!/usr/bin/env python3
"""验证三算法差异、真实指令门禁与修复回归，不将主机模型视为 RTL。"""
import argparse
import csv
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from program_adaptation import coefficients_to_physical

ROOT = Path(__file__).resolve().parents[1]
with (ROOT / "scheme-cases.tsv").open() as stream:
    SPECS = list(csv.DictReader(stream, delimiter="\t"))


class SchemeTests(unittest.TestCase):
    def test_each_scheme_has_distinct_standalone_kernels_and_quick_keyswitch(self):
        for role in ("hadd", "hmul", "keyswitch", "reline", "modswitch", "rotate"):
            rows = [r for r in SPECS if r["role"] == role and r["degree"] == "4096"]
            self.assertEqual({r["scheme"] for r in rows}, {"ckks", "bfv", "bgv"})
            for row in rows:
                self.assertIn(row["scheme"].upper(), row["case_id"])
                self.assertEqual(Path(row["source"]).stem, row["case_id"])
        self.assertEqual(len([r for r in SPECS if r["role"] == "application"]), 8)
        self.assertEqual({r["scheme"] for r in SPECS if r["degree"] == "128" and
                          r["role"] == "keyswitch"}, {"ckks", "bfv", "bgv"})

    def test_all_imports_record_seal_and_program_model_acceptance(self):
        for row in SPECS:
            with self.subTest(case=row["case_id"]):
                data = ARGS.generated / row["program_stem"]
                adaptation = json.loads((data / "AM_ADAPTATION.json").read_text())
                self.assertEqual(adaptation["golden_source"], "modified-SEAL Evaluator")
                self.assertIs(adaptation["program_model_matches_seal"], True)
                self.assertIs(adaptation["rtl_verified"], False)
                self.assertIn("mismatched_limbs=0", (data / "PROGRAM_MODEL.log").read_text())
                c_source = (data / (row["program_stem"] + ".c")).read_text()
                with (data / "dma_relocation_manifest.csv").open() as stream:
                    dma = list(csv.DictReader(stream))
                self.assertEqual(c_source.count("trace_issue("), len(dma))

    def test_bfv_permutation_is_bijective_and_preserves_each_word(self):
        words = struct.pack("<128I", *range(128))
        physical = coefficients_to_physical(words, 128)
        self.assertEqual(coefficients_to_physical(physical, 128), words)
        self.assertNotEqual(physical, words)

    def test_original_ckks_missing_rounding_is_rejected_at_instruction_level(self):
        source = ARGS.generated / "seal_ckks_reline_n4096" / "upstream"
        package = json.loads((source / "package.json").read_text())
        with tempfile.TemporaryDirectory(prefix="scheme-negative-") as temporary:
            result = subprocess.run([
                str(ARGS.model), str(source / package["program_asm"]),
                str(source / package["image"]), str(source / package["dma_relocation_manifest"]),
                str(source / package["golden_manifest"]), str(source),
                str(Path(temporary) / "raw.u32.bin")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("PROGRAM_MODEL_MISMATCH", result.stderr)
        fixed = json.loads((ARGS.generated / "seal_ckks_reline_n4096" / "AM_ADAPTATION.json").read_text())
        self.assertGreater(fixed["ckks_rounded_p_instructions_added"], 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--generated", required=True, type=Path)
    parser.add_argument("--model", required=True, type=Path)
    ARGS, extra = parser.parse_known_args()
    unittest.main(argv=[__file__, *extra])
