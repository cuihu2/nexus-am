#!/usr/bin/env python3
"""小型 mock 验证 subtest 的日志构建隔离；不编译或运行 RISC-V 程序。"""

import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "build_subtests", Path(__file__).with_name("build-subtests.py"))
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class BuildLogModeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hpu-build-log-mode-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.generated = self.root / "generated"
        (self.generated / "inline-asm/mm").mkdir(parents=True)
        (self.generated / "inline-asm/mm/encoder_words.tsv").write_text(
            "word_hex\n0x7000005b\n", encoding="ascii")
        (self.generated / "instruction-data").mkdir()
        (self.generated / "instruction-data/selection.tsv").write_text("mock\n")
        self.commands = []
        self.row = dict(parent_case_id="HPU_IT_DIR_INS_C0_001", subcase="2",
                        subtest_id="HPU_IT_DIR_INS_C0_001__s02_boundary", programs="1",
                        description="边界子项", source="src/03_compute_instructions/01_basic/HPU_IT_DIR_INS_C0_001.c")

    def mock_run(self, command, **kwargs):
        self.commands.append(command)
        if command[0] == "make":
            binary = Path(next(item.split("=", 1)[1] for item in command if item.startswith("BINARY=")))
            binary.with_suffix(".elf").write_bytes(b"mock ELF")
        elif command[0].endswith("objcopy"):
            Path(command[-1]).write_bytes(b"mock BIN")

    def mock_output(self, command, **kwargs):
        if command[0].endswith("objdump"):
            return "80000000 <main>:\n80000000: 7000005b custom2\n"
        if command[:2] == ["git", "-C"]:
            return "mock-am-revision\n"
        raise AssertionError(command)

    def test_modes_have_separate_objects_matching_flags_and_metadata(self):
        output = self.root / "subtests"
        with patch.object(BUILDER, "load_catalog", return_value=[self.row]), \
             patch.object(BUILDER, "check_delivery", return_value="mock-inline-revision"), \
             patch.object(BUILDER, "verify_elf"), \
             patch.object(BUILDER.shutil, "which", return_value="/mock/tool"), \
             patch.object(BUILDER, "run", side_effect=self.mock_run), \
             patch.object(BUILDER.subprocess, "check_output", side_effect=self.mock_output):
            for level, mode, dump, uart in ((1, "minimal", 0, "brief"),
                                             (0, "silent", 0, "none"),
                                             (2, "verbose", 1, "full")):
                with self.subTest(level=level):
                    BUILDER.build(output, self.generated, "riscv64-xs", "mock-", 1, dump, level)
                    command = next(command for command in reversed(self.commands) if command[0] == "make")
                    self.assertIn(f"HPU_LOG_LEVEL={level}", command)
                    self.assertIn(f"HPU_DUMP_RESULTS={dump}", command)
                    self.assertIn("mainargs=subcase=2", command)
                    self.assertIn(f"HPU_DST_DIR={output}/obj/log-{level}-dump-{dump}/{self.row['parent_case_id']}/", command)
                    release = output / "release"
                    metadata = dict(line.split("=", 1) for line in (release / "MANIFEST.txt").read_text().splitlines())
                    self.assertEqual((metadata["log_level"], metadata["log_mode"], metadata["uart_results"]),
                                     (str(level), mode, uart))
                    with (release / "INDEX.tsv").open() as stream:
                        entry = next(csv.DictReader(stream, delimiter="\t"))
                    self.assertEqual((entry["log_level"], entry["log_mode"]), (str(level), mode))
                    self.assertTrue((release / entry["elf"]).is_file())
                    self.assertIn(f"HPU_LOG_LEVEL={level}", (release / "README.md").read_text())

    def test_invalid_modes_fail_before_delivery_or_build(self):
        with patch.object(BUILDER, "load_catalog") as catalog:
            for dump, level in ((1, 0), (1, 1), (0, 3), (2, 2)):
                with self.subTest(dump=dump, level=level):
                    with self.assertRaisesRegex(ValueError, "full results require"):
                        BUILDER.build(self.root / "output", self.generated, "riscv64-xs", "mock-", 1, dump, level)
            catalog.assert_not_called()


if __name__ == "__main__":
    unittest.main()
