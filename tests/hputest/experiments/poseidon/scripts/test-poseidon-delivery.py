#!/usr/bin/env python3
"""检查05组真实库证据、独立用例矩阵及错误oracle拒收。"""
import argparse
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE_SPEC = importlib.util.spec_from_file_location(
    "application_import", Path(__file__).with_name("import-application-package.py"))
IMPORTER = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(IMPORTER)
with (ROOT / "scheme-cases.tsv").open() as stream:
    ROWS = [r for r in csv.DictReader(stream, delimiter="\t")
            if r["program_stem"].startswith("poseidon_")]


class PoseidonTests(unittest.TestCase):
    def test_complete_matrix_and_flat_single_test_sources(self):
        self.assertEqual(len(ROWS), 30)
        self.assertEqual({(r["scheme"], r["degree"], r["role"]) for r in ROWS},
                         {(scheme, n, op) for scheme in ("ckks", "bfv", "bgv")
                          for n in ("128", "4096")
                          for op in ("hadd", "hmul", "reline", "modswitch", "rotate")})
        for row in ROWS:
            source = Path(row["source"])
            self.assertEqual(source.parts[0], "src")
            self.assertEqual(len(source.parts), 3)
            self.assertEqual(source.stem, row["case_id"])
            code = (ROOT / source).read_text()
            self.assertEqual(code.count("hpu_run_" + row["program_stem"] + "()"), 1)
            self.assertIn("application_check_results()", code)
            self.assertIn("application_check_memory()", code)

    def test_each_real_oracle_and_instruction_gate_passed(self):
        commit = (ARGS.generated / "POSEIDON_COMMIT").read_text().strip()
        for row in ROWS:
            with self.subTest(case=row["case_id"]):
                data = ARGS.generated / row["program_stem"]
                report = json.loads((data / "POSEIDON_ORACLE.json").read_text())
                adaptation = json.loads((data / "AM_ADAPTATION.json").read_text())
                self.assertEqual(report["revision"], commit)
                self.assertEqual(adaptation["poseidon_oracle"], report)
                self.assertIs(adaptation["program_model_matches_seal"], True)
                self.assertIs(adaptation["rtl_verified"], False)
                self.assertIn("mismatched_limbs=0", (data / "PROGRAM_MODEL.log").read_text())
                words = [int(w, 2) for w in (data / (row["program_stem"] + ".inst32")).read_text().split()]
                self.assertEqual(words[-1], 0x7000005B)
                self.assertEqual(words.count(0x7000005B), 1)
                self.assertTrue(all(w & 0x7F in (0x5B, 0x2B) for w in words))
                with tempfile.TemporaryDirectory(prefix="poseidon-oracle-") as temporary:
                    source = Path(temporary) / row["program_stem"]
                    source.with_name(source.name + ".poseidon.json").write_text(json.dumps(report))
                    self.assertEqual(IMPORTER.validate_poseidon(source, row, commit), report)

    def test_bfv_multiply_does_not_falsely_claim_raw_identity(self):
        for row in ROWS:
            report = json.loads((ARGS.generated / row["program_stem"] / "POSEIDON_ORACLE.json").read_text())
            semantic = row["scheme"] == "bfv" and row["role"] == "hmul"
            self.assertEqual(report["plaintext_coefficients_compared"],
                             2 * int(row["degree"]) if semantic else 0)
            if not semantic:
                self.assertEqual(report["raw_word_mismatches"], 0)
            else:
                self.assertIn("exact decrypted BFV polynomial", report["comparison"])

    def test_failed_wrong_version_or_wrong_api_cannot_be_published(self):
        row = ROWS[0]
        report = json.loads((ARGS.generated / row["program_stem"] / "POSEIDON_ORACLE.json").read_text())
        with tempfile.TemporaryDirectory(prefix="poseidon-reject-") as temporary:
            source = Path(temporary) / row["program_stem"]
            sidecar = source.with_name(source.name + ".poseidon.json")
            for field, value in (("status", "FAIL"), ("revision", "0" * 40),
                                 ("api", "fake::add"), ("degree", 256),
                                 ("raw_word_mismatches", 1), ("rotation_steps", 1)):
                changed = dict(report, **{field: value})
                sidecar.write_text(json.dumps(changed))
                with self.subTest(field=field), self.assertRaises(ValueError):
                    IMPORTER.validate_poseidon(source, row, report["revision"])
            sidecar.unlink()
            with self.assertRaises(FileNotFoundError):
                IMPORTER.validate_poseidon(source, row, report["revision"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--generated", type=Path, required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])
