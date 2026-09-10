#!/usr/bin/env python3
"""用小型临时构建清单验证按章节打包，不编译或运行 HPU 指令。"""

import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "package_chapters", Path(__file__).with_name("package-chapters.py"))
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)


class ChapterPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hpu-package-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.artifact = self.root / "artifact"
        self.artifact.mkdir()
        (self.artifact / "provenance").mkdir()
        (self.artifact / "provenance" / "PRODUCER_COMMIT").write_text(
            "producer-revision\n", encoding="utf-8")

    def row(self, case_id="HPU_IT_DIR_INS_C0_001", group="core",
            qualifier="software-self-check", chapter="03_compute_instructions"):
        return {"group": group, "qualifier": qualifier, "case_id": case_id,
                "source": f"src/{chapter}/01_basic/{case_id}.c"}

    def fixture(self, rows, selection="case:test", uart_results="brief", dump_results="0"):
        with (self.artifact / "CASE_MANIFEST.tsv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=("group", "qualifier", "case_id", "source"), delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)
        blocked = [row for row in rows if row["qualifier"] == "blocked-not-issued"]
        with (self.artifact / "NOT_QUALIFIED.tsv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=("case_id", "source", "reason"), delimiter="\t")
            writer.writeheader()
            for row in blocked:
                writer.writerow({"case_id": row["case_id"], "source": row["source"],
                                 "reason": "缺少已验证的 golden；不得发布占位程序"})
        (self.artifact / "MANIFEST.txt").write_text(
            f"selection={selection}\ncase_count={len(rows)}\n"
            f"uart_results={uart_results}\nhpu_dump_results={dump_results}\n"
            f"not_qualified_count={len(blocked)}\n", encoding="utf-8")
        for row in rows:
            relative = Path(row["source"]).relative_to("src")
            if ".." in relative.parts:
                continue  # 恶意路径测试只篡改清单，绝不向临时目录外创建文件。
            for extension in PACKAGER.EXTENSIONS:
                target = self.artifact / row["group"] / relative.with_suffix(extension)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((row["case_id"] + extension).encode())

    def index(self, release):
        return PACKAGER.read_tsv(release / "INDEX.tsv", PACKAGER.INDEX_FIELDS)

    def test_merge_groups_into_one_chapter(self):
        self.fixture([self.row(), self.row("HPU_IT_DIR_INS_C0_005", "transform"),
                      self.row("HPU_IT_DIR_CMB_004", "fhe", chapter="04_composite_instruction_sequences")])
        release = PACKAGER.package(self.artifact)
        self.assertEqual(release, self.root / "release")
        self.assertTrue((release / "03_compute_instructions/01_basic/HPU_IT_DIR_INS_C0_001.elf").is_file())
        self.assertTrue((release / "03_compute_instructions/01_basic/HPU_IT_DIR_INS_C0_005.elf").is_file())
        self.assertFalse((release / "core").exists())
        self.assertFalse((release / "transform").exists())
        self.assertEqual(len(list(release.rglob("PRODUCER_COMMIT"))), 1)
        self.assertEqual(len(self.index(release)), 3)
        for name in PACKAGER.MANIFESTS:
            self.assertEqual((release / name).read_bytes(), (self.artifact / name).read_bytes())

    def test_blocked_is_index_only_and_reason_is_preserved(self):
        blocked = self.row("HPU_IT_DIR_INS_C0_006", "transform", "blocked-not-issued")
        self.fixture([self.row(), blocked])
        release = PACKAGER.package(self.artifact)
        rows = {row["case_id"]: row for row in self.index(release)}
        entry = rows[blocked["case_id"]]
        self.assertEqual(entry["publish_status"], "BLOCKED_NOT_PUBLISHED")
        self.assertIn("缺少已验证", entry["notes"])
        for field in ("elf", "bin", "disassembly"):
            self.assertEqual(entry[field], "")
        self.assertEqual(list(release.rglob("HPU_IT_DIR_INS_C0_006.*")), [])
        self.assertEqual(len(list(release.rglob("*.elf"))), 1)

    def test_missing_binary_does_not_create_release(self):
        self.fixture([self.row()])
        next(self.artifact.rglob("*.bin")).unlink()
        with self.assertRaisesRegex(ValueError, "missing or empty"):
            PACKAGER.package(self.artifact)
        self.assertFalse((self.root / "release").exists())

    def test_escape_is_rejected(self):
        row = self.row()
        row["source"] = "src/03_compute_instructions/../../HPU_IT_DIR_INS_C0_001.c"
        self.fixture([row])
        with self.assertRaisesRegex(ValueError, "escaping source"):
            PACKAGER.package(self.artifact)

    def test_duplicate_id_is_rejected(self):
        self.fixture([self.row(), self.row(group="transform")])
        with self.assertRaisesRegex(ValueError, "duplicate case"):
            PACKAGER.package(self.artifact)

    def test_symlink_binary_is_rejected(self):
        self.fixture([self.row()])
        binary = next(self.artifact.rglob("*.bin"))
        binary.unlink()
        target = self.root / "other.bin"
        target.write_bytes(b"not from artifact")
        binary.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "symlink"):
            PACKAGER.package(self.artifact)

    def test_hold_and_expected_failure_are_explicit(self):
        self.fixture([
            self.row("01_dload_hold", qualifier="waveform-hold", chapter="00_bringup"),
            self.row("02_return_1", qualifier="termination-probe-fail", chapter="00_bringup"),
            self.row("01_return_0", qualifier="termination-probe-pass", chapter="00_bringup"),
        ])
        release = PACKAGER.package(self.artifact)
        rows = {row["case_id"]: row for row in self.index(release)}
        self.assertIn("cycle-limit", rows["01_dload_hold"]["notes"])
        self.assertEqual(rows["02_return_1"]["publish_status"], "EXPECTED_FAIL_PROBE_RETURN_1")
        self.assertIn("return 0", rows["01_return_0"]["notes"])
        self.assertIn("不验证 HPU 功能", rows["01_return_0"]["notes"])
        self.assertIn("不表示 IT/VCS 已通过", (release / "README.md").read_text())

    def test_single_case_does_not_require_all_nine(self):
        self.fixture([self.row()])
        PACKAGER.package(self.artifact)

    def test_all_requires_full_selection(self):
        self.fixture([self.row()])
        with self.assertRaisesRegex(ValueError, "selection=all"):
            PACKAGER.package(self.artifact, require_all=True)

    def test_all_rejects_missing_005_006(self):
        self.fixture([self.row(f"HPU_IT_DIR_INS_C0_{i:03d}") for i in (1, 2, 3, 4, 7, 8, 9)], selection="all")
        with self.assertRaisesRegex(ValueError, "all nine"):
            PACKAGER.package(self.artifact, require_all=True)

    def test_all_publishes_all_nine_in_same_chapter(self):
        self.fixture([self.row(f"HPU_IT_DIR_INS_C0_{i:03d}", "transform" if i in (5, 6) else "core")
                      for i in range(1, 10)], selection="all")
        release = PACKAGER.package(self.artifact, require_all=True)
        self.assertEqual(len(list((release / "03_compute_instructions").rglob("*.elf"))), 9)

    def diagnostic_rows(self):
        return [self.row(f"HPU_IT_DIR_INS_C0_{i:03d}",
                         "transform" if i in (5, 6) else "core")
                for i in range(1, 10)] + [
            self.row(f"HPU_IT_DIR_CMB_{i:03d}", "transform",
                     chapter="04_composite_instruction_sequences")
            for i in range(1, 4)]

    def test_diagnostic_publishes_twelve_full_uart_cases(self):
        self.fixture(self.diagnostic_rows(), selection="diagnostic",
                     uart_results="full", dump_results="1")
        release = PACKAGER.package(self.artifact, require_diagnostic=True)
        self.assertEqual(len(list(release.rglob("*.elf"))), 12)
        self.assertEqual({row["case_id"] for row in self.index(release)},
                         PACKAGER.DIAGNOSTIC_IDS)
        self.assertTrue(all("全量 UART" in row["notes"] for row in self.index(release)))
        readme = (release / "README.md").read_text(encoding="utf-8")
        self.assertIn("不是加速包", readme)
        self.assertIn("4096", readme)
        self.assertIn("nexus-am-hpu-uart-results", readme)

    def test_diagnostic_rejects_brief_artifacts(self):
        self.fixture(self.diagnostic_rows(), selection="diagnostic")
        with self.assertRaisesRegex(ValueError, "requires full UART"):
            PACKAGER.package(self.artifact, require_diagnostic=True)

    def test_diagnostic_requires_explicit_selection_and_flag(self):
        self.fixture(self.diagnostic_rows(), selection="diagnostic",
                     uart_results="full", dump_results="1")
        with self.assertRaisesRegex(ValueError, "must be used together"):
            PACKAGER.package(self.artifact)
        self.fixture(self.diagnostic_rows(), selection="all",
                     uart_results="full", dump_results="1")
        with self.assertRaisesRegex(ValueError, "must be used together"):
            PACKAGER.package(self.artifact, require_diagnostic=True)

    def test_diagnostic_rejects_missing_case(self):
        self.fixture(self.diagnostic_rows()[:-1], selection="diagnostic",
                     uart_results="full", dump_results="1")
        with self.assertRaisesRegex(ValueError, "exactly the twelve"):
            PACKAGER.package(self.artifact, require_diagnostic=True)

    def test_diagnostic_rejects_blocked_placeholder(self):
        rows = self.diagnostic_rows()
        rows[-1]["qualifier"] = "blocked-not-issued"
        self.fixture(rows, selection="diagnostic", uart_results="full", dump_results="1")
        with self.assertRaisesRegex(ValueError, "exactly the twelve"):
            PACKAGER.package(self.artifact, require_diagnostic=True)

    def test_diagnostic_rejects_wrong_chapter(self):
        rows = self.diagnostic_rows()
        rows[-1] = self.row("HPU_IT_DIR_CMB_003", "transform",
                            chapter="00_bringup")
        self.fixture(rows, selection="diagnostic", uart_results="full", dump_results="1")
        with self.assertRaisesRegex(ValueError, "exactly the twelve"):
            PACKAGER.package(self.artifact, require_diagnostic=True)

    def test_uart_macro_and_metadata_must_agree(self):
        self.fixture([self.row()], uart_results="full", dump_results="0")
        with self.assertRaisesRegex(ValueError, "inconsistent UART"):
            PACKAGER.package(self.artifact)

    def test_default_all_rejects_full_uart(self):
        self.fixture(self.diagnostic_rows(), selection="all", uart_results="full", dump_results="1")
        with self.assertRaisesRegex(ValueError, "requires brief UART"):
            PACKAGER.package(self.artifact, require_all=True)

    def test_regular_package_identifies_brief_default(self):
        self.fixture([self.row()])
        release = PACKAGER.package(self.artifact)
        readme = (release / "README.md").read_text(encoding="utf-8")
        self.assertIn("默认 UART 摘要版", readme)
        self.assertIn("HPU_DUMP_RESULTS=0", readme)

    def test_uart_export_tool_is_included_without_following_symlinks(self):
        self.fixture([self.row()])
        tools = self.artifact / "tools"
        tools.mkdir()
        parser = tools / "parse-uart-results.py"
        parser.write_text("# test parser\n", encoding="utf-8")
        release = PACKAGER.package(self.artifact)
        self.assertEqual((release / "tools" / parser.name).read_text(), "# test parser\n")
        (tools / "alias.py").symlink_to(parser)
        with self.assertRaisesRegex(ValueError, "symlink"):
            PACKAGER.package(self.artifact)

    def test_existing_unmarked_directory_is_preserved(self):
        self.fixture([self.row()])
        release = self.root / "release"
        release.mkdir()
        note = release / "user-note.txt"
        note.write_text("keep me", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "unmarked"):
            PACKAGER.package(self.artifact)
        self.assertEqual(note.read_text(), "keep me")

    def test_existing_generated_release_with_extra_file_is_preserved(self):
        self.fixture([self.row()])
        release = PACKAGER.package(self.artifact)
        note = release / "user-note.txt"
        note.write_text("keep me", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "untracked"):
            PACKAGER.package(self.artifact)
        self.assertEqual(note.read_text(), "keep me")

    def test_safe_replacement_retains_previous_package(self):
        self.fixture([self.row()])
        release = PACKAGER.package(self.artifact)
        PACKAGER.package(self.artifact)
        backups = list(self.root.glob(".release-previous-*"))
        self.assertEqual(len(backups), 1)
        self.assertTrue((backups[0] / "INDEX.tsv").is_file())
        self.assertEqual(len(list(release.rglob("*.elf"))), 1)
        self.assertEqual(list(self.root.glob(".release-staging-*")), [])


if __name__ == "__main__":
    unittest.main()
