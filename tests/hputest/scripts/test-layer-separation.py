#!/usr/bin/env python3
"""04只做基本/组合算子，05做算法库；防止重复条目重新混入默认清单。"""
import csv
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def rows(name):
    with (ROOT / name).open() as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


class LayerTests(unittest.TestCase):
    def test_04_contains_only_basic_operators_and_orchestration(self):
        cases = [r for r in rows("cases.tsv") if r["source"].startswith("src/04_")]
        self.assertEqual(len(cases), 11)
        self.assertTrue(all("/02_algorithm_library_operators/" not in r["source"] for r in cases))
        specs = [r for r in rows("scheme-cases.tsv") if r["source"].startswith("src/04_")]
        self.assertEqual(len(specs), 6)
        self.assertEqual({r["role"] for r in specs}, {"keyswitch"})
        self.assertTrue(all(not r["case_id"].startswith("HPU_IT_LIB_") for r in cases))

    def test_05_has_poseidon_matrix_distinct_parameter_regressions_and_pending(self):
        cases = [r for r in rows("cases.tsv") if r["source"].startswith("src/05_")]
        self.assertEqual(len(cases), 39)
        self.assertTrue(all(r["case_id"].startswith("HPU_IT_LIB_") for r in cases))
        specs = [r for r in rows("scheme-cases.tsv") if r["source"].startswith("src/05_")]
        poseidon = [r for r in specs if r["program_stem"].startswith("poseidon_")]
        regression = [r for r in specs if not r["program_stem"].startswith("poseidon_")]
        self.assertEqual(len(poseidon), 30)
        self.assertEqual({(r["scheme"], r["degree"], r["role"]) for r in poseidon},
                         {(s, n, op) for s in ("ckks", "bfv", "bgv") for n in ("128", "4096")
                          for op in ("hadd", "hmul", "reline", "modswitch", "rotate")})
        self.assertEqual({r["program_stem"] for r in regression},
                         {"bgv_hadd_n4096", "bgv_hmul_n4096", "bgv_reline_n4096",
                          "bgv_modswitch_n4096", "ckks_rotate_n4096", "bfv_rotate_n4096",
                          "bgv_rotate_n4096"})
        for row in regression:
            self.assertIn("/04_parameter_regression/", row["source"])
            self.assertTrue("_CF" in row["case_id"] or "_GEN3_STEP1_" in row["case_id"])
            self.assertIn("不宣称调用Poseidon API", (ROOT / row["source"]).read_text())
        pending = [r for r in cases if r["qualifier"] == "blocked-not-issued"]
        self.assertEqual(len(pending), 2)
        for row in pending:
            self.assertIn("/05_pending_interfaces/", row["source"])
            self.assertIn("case_not_qualified(__FILE__)", (ROOT / row["source"]).read_text())

    def test_removed_duplicates_have_direct_aliases_and_no_second_source(self):
        live = {r["case_id"]: r for r in rows("cases.tsv")}
        aliases = rows("case-aliases.tsv")
        self.assertEqual(len({r["previous_case_id"] for r in aliases}), len(aliases))
        self.assertTrue(all(r["case_id"] in live for r in aliases))
        alias_map = {r["previous_case_id"]: r["case_id"] for r in aliases}
        migrations = rows("layer-migration.tsv")
        self.assertEqual(len(migrations), 17)
        duplicates = [r for r in migrations if r["disposition"] == "DEDUPLICATED"]
        self.assertEqual(len(duplicates), 8)
        for row in migrations:
            self.assertEqual(alias_map[row["previous_case_id"]], row["case_id"])
            self.assertEqual(live[row["case_id"]]["source"], row["source"])
            self.assertFalse((ROOT / row["previous_source"]).exists())
        active_stems = {r["program_stem"] for r in rows("scheme-cases.tsv")}
        self.assertFalse(active_stems & {f"{s}_{op}_n4096" for s in ("ckks", "bfv")
                                       for op in ("hadd", "hmul", "reline", "modswitch")})

    def test_keyswitch_is_not_reline_or_hmul(self):
        fixture = (ROOT / "tools/hpu-scheme-cases/fixture.hpp").read_text()
        self.assertIn('if (op == "keyswitch")', fixture)
        self.assertIn("std::fill(input.data(0), input.data(0) + 2 * words, 0)", fixture)
        self.assertTrue(all(r["source"].startswith("src/05_") for r in rows("scheme-cases.tsv")
                            if r["role"] in ("reline", "hmul")))


if __name__ == "__main__":
    unittest.main()
