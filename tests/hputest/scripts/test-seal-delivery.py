#!/usr/bin/env python3
"""默认库验证必须用inline-asm的modified-SEAL，BFV也不能以明文等价放行。"""
import argparse
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("seal_import", Path(__file__).with_name("import-application-package.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)
with (ROOT / "scheme-cases.tsv").open() as stream:
    ROWS = [r for r in csv.DictReader(stream, delimiter="\t")
            if r["source"].startswith("src/05_algorithm_library/")]
STANDARD_ROWS = [r for r in ROWS if "/04_parameter_regression/" not in r["source"]]
REGRESSION_ROWS = [r for r in ROWS if "/04_parameter_regression/" in r["source"]]


class SealTests(unittest.TestCase):
    def test_complete_seal_matrix_and_single_run_sources(self):
        self.assertEqual(len(ROWS), 37)
        self.assertEqual(len(REGRESSION_ROWS), 7)
        self.assertEqual({(r["scheme"], r["degree"], r["role"]) for r in STANDARD_ROWS},
                         {(s, n, op) for s in ("ckks", "bfv", "bgv") for n in ("128", "4096")
                          for op in ("hadd", "hmul", "reline", "modswitch", "rotate")})
        for row in ROWS:
            source = ROOT / row["source"]
            self.assertTrue(row["source"].startswith("src/05_algorithm_library/"))
            self.assertTrue(row["case_id"].startswith("HPU_IT_LIB_SEAL_"))
            self.assertEqual(source.stem, row["case_id"])
            self.assertEqual(source.read_text().count("hpu_run_" + row["program_stem"] + "()"), 1)

    def test_library_source_and_real_instruction_evidence(self):
        commit = (ARGS.generated / "PRODUCER_COMMIT").read_text().strip()
        for row in ROWS:
            with self.subTest(case=row["case_id"]):
                data = ARGS.generated / row["program_stem"]
                report = json.loads((data / "SEAL_ORACLE.json").read_text())
                adaptation = json.loads((data / "AM_ADAPTATION.json").read_text())
                self.assertEqual(report["producer_commit"], commit)
                self.assertEqual(report["library"], "inline-asm/third_party/modified-SEAL")
                self.assertEqual(report["comparison"], "all raw physical words")
                self.assertEqual(report["raw_word_mismatches"], 0)
                regression = "/04_parameter_regression/" in row["source"]
                self.assertEqual(report["initial_correction_factors"],
                                 [3, 5] if regression and row["scheme"] == "bgv" else [1, 1])
                self.assertEqual(report["rotation_steps"],
                                 (1 if regression else int(row["degree"]) // 4)
                                 if row["role"] == "rotate" else 0)
                self.assertEqual(adaptation["seal_oracle"], report)
                self.assertNotIn("poseidon_oracle", adaptation)
                self.assertFalse((data / "POSEIDON_ORACLE.json").exists())
                self.assertIs(adaptation["program_model_matches_seal"], True)
                self.assertIs(adaptation["rtl_verified"], False)
                self.assertIn("mismatched_limbs=0", (data / "PROGRAM_MODEL.log").read_text())
                words = [int(w, 2) for w in (data / (row["program_stem"] + ".inst32")).read_text().split()]
                self.assertEqual(words[-1], 0x7000005B)
                self.assertEqual(words.count(0x7000005B), 1)
                with tempfile.TemporaryDirectory(prefix="seal-oracle-") as temporary:
                    source = Path(temporary) / row["program_stem"]
                    source.with_name(source.name + ".seal.json").write_text(json.dumps(report))
                    self.assertEqual(IMPORTER.validate_seal(source, row, commit), report)

    def test_bfv_cannot_pass_with_decrypted_equivalence(self):
        row = next(r for r in ROWS if r["scheme"] == "bfv" and r["role"] == "hmul")
        report = json.loads((ARGS.generated / row["program_stem"] / "SEAL_ORACLE.json").read_text())
        with tempfile.TemporaryDirectory(prefix="seal-reject-") as temporary:
            source = Path(temporary) / row["program_stem"]
            sidecar = source.with_name(source.name + ".seal.json")
            for field, value in (("comparison", "exact decrypted BFV polynomial; SEAL physical golden"),
                                 ("raw_word_mismatches", 1), ("status", "FAIL"),
                                 ("producer_commit", "0" * 40), ("api", "Poseidon::multiply_relin")):
                sidecar.write_text(json.dumps(dict(report, **{field: value})))
                with self.subTest(field=field), self.assertRaises(ValueError):
                    IMPORTER.validate_seal(source, row, report["producer_commit"])

    def test_hmul_rejects_multiply_without_relinearize(self):
        row = next(r for r in ROWS if r["scheme"] == "ckks" and r["role"] == "hmul")
        with self.assertRaisesRegex(ValueError, "HMUL graph mismatch"):
            IMPORTER.validate_operation_graph(
                row, [{"kind": "multiply", "component_count": 2, "operation_index": 0}])
        IMPORTER.validate_operation_graph(row, [
            {"kind": "multiply", "component_count": 3, "operation_index": 0},
            {"kind": "relinearize", "component_count": 2, "operation_index": 1},
        ])
        IMPORTER.validate_operation_graph(
            row, [{"kind": "multiply_relinearize", "component_count": 2, "operation_index": 0}])

    def test_poseidon_is_not_a_default_dependency_or_case(self):
        for path in ("scheme-cases.tsv", "cases.tsv", "Makefile", "scripts/prepare-inline-asm-mm.sh"):
            self.assertNotIn("poseidon", (ROOT / path).read_text().lower())
        self.assertFalse(hasattr(IMPORTER, "validate_poseidon"))
        self.assertTrue((ROOT / "experiments/poseidon/scheme-cases.tsv").is_file())
        self.assertTrue((ROOT / "experiments/poseidon/tools/poseidon_bridge.cpp").is_file())
        cmake = (ROOT / "tools/hpu-scheme-cases/CMakeLists.txt").read_text()
        self.assertNotIn("poseidon", cmake.lower())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--generated", type=Path, required=True)
    ARGS, extra = parser.parse_known_args()
    unittest.main(argv=[__file__, *extra])
