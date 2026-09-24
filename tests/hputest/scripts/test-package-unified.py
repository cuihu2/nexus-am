#!/usr/bin/env python3
"""用临时伪产物验证统一 HPU 下载包，不调用交叉编译器。"""

import csv
import importlib.util
from pathlib import Path, PurePosixPath
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("package-unified.py")
SPEC = importlib.util.spec_from_file_location("package_unified", SCRIPT)
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)
RUNNER_SCRIPT = Path(__file__).with_name("run-subtests.py")
RUNNER_SPEC = importlib.util.spec_from_file_location("run_subtests_for_unified_test", RUNNER_SCRIPT)
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)
TEST_ROOT = Path(__file__).resolve().parents[1]


class UnifiedPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hpu-unified-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.workloads = self.make_workloads("workloads", silent=False)
        self.silent_workloads = self.make_workloads("silent-workloads", silent=True)
        self.subtests = self.make_subtests("subtests", silent=False)
        self.silent_subtests = self.make_subtests("silent-subtests", silent=True)
        self.output = self.root / "unified"

    @staticmethod
    def rows(path):
        with path.open(encoding="utf-8", newline="") as stream:
            return list(csv.DictReader(stream, delimiter="\t"))

    @staticmethod
    def write_tsv(path, fields, rows):
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)

    @staticmethod
    def write_artifacts(release, base, identity, silent):
        paths = {}
        for field, extension in PACKAGER.FILE_FIELDS:
            relative = base.with_suffix(extension)
            target = release / Path(relative.as_posix())
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(f"{identity}:{'silent' if silent else 'normal'}:{field}".encode())
            paths[field] = relative.as_posix()
        return paths

    @staticmethod
    def common_tree(release, subtests=False):
        provenance = release / "provenance"
        provenance.mkdir()
        (provenance / "PRODUCER_COMMIT").write_text("producer-commit\n", encoding="utf-8")
        tools = release / "tools"
        tools.mkdir()
        (tools / "parse-uart-results.py").write_text("# parser\n", encoding="utf-8")
        if subtests:
            (tools / "run-subtests.py").write_text("# runner\n", encoding="utf-8")

    def make_workloads(self, name, silent):
        release = self.root / name / "release"
        release.mkdir(parents=True)
        self.common_tree(release)
        mode = "silent" if silent else "minimal"
        uart = "none" if silent else "brief"
        level = "0" if silent else "1"
        (release / "MANIFEST.txt").write_text(
            "repository=test/repository\nrevision=am-revision\narch=riscv64-xs\n"
            "selection=all\nmainargs=all\n"
            f"log_level={level}\nlog_mode={mode}\nuart_results={uart}\n"
            "hpu_dump_results=0\ncase_count=62\nnot_qualified_count=14\n"
            "inline_asm_commit=producer-commit\n"
            "hpu_seal_commit=hpu-seal-producer-commit\n", encoding="utf-8")
        rows = []
        for source in self.rows(TEST_ROOT / "cases.tsv"):
            relative_source = PurePosixPath(source["source"])
            chapter = relative_source.parts[1]
            base = PurePosixPath(*relative_source.parts[1:]).with_suffix("")
            blocked = source["qualifier"] == "blocked-not-issued"
            row = {
                "chapter": chapter, "case_id": source["case_id"],
                "qualifier": source["qualifier"],
                "publish_status": "BLOCKED_NOT_PUBLISHED" if blocked else "BUILD_READY_NOT_IT_PASS",
                "source": source["source"], "elf": "", "bin": "", "disassembly": "",
                "notes": "blocked reason" if blocked else f"{mode} test artifact",
            }
            if not blocked:
                row.update(self.write_artifacts(release, base, source["case_id"], silent))
            rows.append(row)
        self.write_tsv(release / "INDEX.tsv", PACKAGER.WORKLOAD_FIELDS, rows)
        return release

    def make_subtests(self, name, silent):
        release = self.root / name / "release"
        release.mkdir(parents=True)
        self.common_tree(release, subtests=True)
        mode = "silent" if silent else "minimal"
        uart = "none" if silent else "brief"
        level = "0" if silent else "1"
        (release / "MANIFEST.txt").write_text(
            "format=hpu-subtests-v1\nrevision=am-revision\ninline_asm_commit=producer-commit\n"
            "arch=riscv64-xs\nparent_cases=9\nsubtests=37\nprograms=40\n"
            f"hpu_dump_results=0\nuart_results={uart}\nlog_level={level}\nlog_mode={mode}\n"
            "execution=one-independent-simv-per-elf\nqualification=BUILD_READY_NOT_IT_PASS\n",
            encoding="utf-8")
        catalog = self.rows(TEST_ROOT / "subtests" / "cases.tsv")
        self.write_tsv(release / "cases.tsv", PACKAGER.SUBTEST_FIELDS[:6], catalog)
        rows = []
        for source in catalog:
            base = PurePosixPath("03_compute_instructions") / source["subtest_id"]
            row = dict(source)
            row.update({
                "mainargs": f"subcase={source['subcase']}",
                "build_status": "BUILD_READY_NOT_IT_PASS",
                "log_level": level, "log_mode": mode,
            })
            row.update(self.write_artifacts(release, base, source["subtest_id"], silent))
            rows.append(row)
        self.write_tsv(release / "INDEX.tsv", PACKAGER.SUBTEST_FIELDS, rows)
        return release

    def package(self):
        return PACKAGER.package(self.workloads, self.silent_workloads,
                                self.subtests, self.silent_subtests, self.output)

    def test_one_tree_contains_normal_and_silent_and_replaces_03_parents(self):
        release = self.package()
        package = release / "hputest"
        self.assertEqual(len(list(package.rglob("*.elf"))), 152)
        normal = package / "01_configuration/01_hpu_register_access/HPU_IT_DIR_CFG_001.elf"
        silent = normal.with_name("HPU_IT_DIR_CFG_001_silent.elf")
        self.assertTrue(normal.is_file())
        self.assertTrue(silent.is_file())
        self.assertFalse(any(path.name == "HPU_IT_DIR_INS_C0_001.elf"
                             for path in (package / "03_compute_instructions").rglob("*.elf")))
        subtest = "HPU_IT_DIR_INS_C0_001__s00_basic_dst_p2"
        self.assertTrue((package / "03_compute_instructions" / f"{subtest}.elf").is_file())
        self.assertTrue((package / "03_compute_instructions" / f"{subtest}_silent.elf").is_file())
        rows = PACKAGER.read_tsv(package / "INDEX.tsv", PACKAGER.INDEX_FIELDS)
        self.assertEqual(len(rows), 166)
        self.assertEqual(sum(bool(row["elf"]) for row in rows), 152)
        self.assertEqual(sum(row["publish_status"] == "BLOCKED_NOT_PUBLISHED" for row in rows), 14)
        self.assertEqual({row["variant"] for row in rows if row["elf"]}, {"normal", "silent"})
        normal_cases = RUNNER.read_index(package, "normal")
        silent_cases = RUNNER.read_index(package, "silent")
        self.assertEqual((len(normal_cases), len(silent_cases)), (37, 37))
        self.assertTrue(all(name.endswith("_silent.elf") for _case_id, name, _path in silent_cases))

    def test_wrong_input_mode_is_rejected(self):
        manifest = self.silent_workloads / "MANIFEST.txt"
        manifest.write_text(manifest.read_text(encoding="utf-8").replace(
            "log_level=0", "log_level=1"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "wrong build mode"):
            self.package()

    def test_inconsistent_hpu_seal_commit_is_rejected(self):
        manifest = self.silent_workloads / "MANIFEST.txt"
        manifest.write_text(manifest.read_text(encoding="utf-8").replace(
            "hpu_seal_commit=hpu-seal-producer-commit",
            "hpu_seal_commit=other-hpu-seal-producer-commit"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "hpu_seal_commit"):
            self.package()

    def test_inconsistent_subtest_row_mode_is_rejected(self):
        rows = PACKAGER.read_tsv(self.silent_subtests / "INDEX.tsv", PACKAGER.SUBTEST_FIELDS)
        rows[0]["log_level"] = "1"
        self.write_tsv(self.silent_subtests / "INDEX.tsv", PACKAGER.SUBTEST_FIELDS, rows)
        with self.assertRaisesRegex(ValueError, "inconsistent row mode"):
            self.package()

    def test_revision_mismatch_is_rejected(self):
        manifest = self.subtests / "MANIFEST.txt"
        manifest.write_text(manifest.read_text(encoding="utf-8").replace(
            "revision=am-revision", "revision=other"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "disagree on revision"):
            self.package()

    def test_missing_input_file_is_rejected(self):
        next(self.silent_subtests.rglob("*.bin")).unlink()
        with self.assertRaisesRegex(ValueError, "missing or empty"):
            self.package()

    def test_index_drift_is_rejected(self):
        rows = PACKAGER.read_tsv(self.silent_subtests / "INDEX.tsv", PACKAGER.SUBTEST_FIELDS)
        rows[0]["description"] = "different meaning"
        self.write_tsv(self.silent_subtests / "INDEX.tsv", PACKAGER.SUBTEST_FIELDS, rows)
        with self.assertRaisesRegex(ValueError, "do not describe the same cases"):
            self.package()

    def test_provenance_drift_is_rejected(self):
        (self.silent_workloads / "provenance" / "PRODUCER_COMMIT").write_text(
            "other\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "workload provenance differs"):
            self.package()

    def test_unowned_output_is_preserved(self):
        self.output.mkdir()
        note = self.output / "user-note.txt"
        note.write_text("keep me", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "unowned"):
            self.package()
        self.assertEqual(note.read_text(encoding="utf-8"), "keep me")

    def test_output_cannot_overlap_an_input_release(self):
        with self.assertRaisesRegex(ValueError, "must not overlap"):
            PACKAGER.package(self.workloads, self.silent_workloads,
                             self.subtests, self.silent_subtests,
                             self.workloads / "nested-output")
        self.assertFalse((self.workloads / "nested-output").exists())

    def test_generated_release_with_extra_file_is_preserved(self):
        release = self.package()
        extra = release / "hputest" / "user-note.txt"
        extra.write_text("keep me", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "untracked"):
            self.package()
        self.assertEqual(extra.read_text(encoding="utf-8"), "keep me")


if __name__ == "__main__":
    unittest.main()
