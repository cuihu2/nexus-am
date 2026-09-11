#!/usr/bin/env python3
"""用小型 TSV/ELF fixture 检查独立 subtest，不执行 HPU 或 RISC-V 代码。"""

import csv
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import subtest_support as SUPPORT


COUNTS = (4, 4, 6, 4, 6, 6, 2, 3, 2)


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hpu-subtest-catalog-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "subtests").mkdir()
        self.parents = []
        self.rows = []
        for number, count in enumerate(COUNTS, start=1):
            parent = f"HPU_IT_DIR_INS_C0_{number:03d}"
            source = f"src/03_compute_instructions/01_basic/{parent}.c"
            path = self.root / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                "/* progress_begin(__FILE__, 99U); 注释不是有效调用。 */\n"
                f"int main(void) {{ return progress_begin(__FILE__, {count}U); }}\n",
                encoding="utf-8")
            self.parents.append({"group": "core", "qualifier": "software-self-check",
                                 "case_id": parent, "source": source})
            for index in range(count):
                self.rows.append({"parent_case_id": parent, "subcase": str(index),
                                  "subtest_id": f"{parent}__s{index:02d}_variant_{index}",
                                  "programs": "2" if number == 8 else "1",
                                  "description": f"独立子项 {index}", "source": source})

    def write(self):
        for relative, rows, fields in (
                ("cases.tsv", self.parents, SUPPORT.PARENT_FIELDS),
                ("subtests/cases.tsv", self.rows, SUPPORT.CATALOG_FIELDS)):
            with (self.root / relative).open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
                writer.writeheader()
                writer.writerows(rows)

    def load(self):
        self.write()
        return SUPPORT.load_catalog(self.root)

    def test_complete_catalog_preserves_order_and_strings(self):
        self.assertEqual(self.load(), self.rows)
        self.assertEqual(len(self.rows), 37)
        self.assertEqual(sum(int(row["programs"]) for row in self.rows), 40)
        self.assertTrue(all(isinstance(row["subcase"], str) for row in self.rows))

    def test_wrong_parent(self):
        self.rows[0]["parent_case_id"] = "HPU_IT_DIR_CMB_001"
        with self.assertRaisesRegex(ValueError, "unknown parent"):
            self.load()

    def test_parent_must_be_ready_and_match_source(self):
        self.parents[0]["qualifier"] = "blocked-not-issued"
        with self.assertRaisesRegex(ValueError, "not a software-self-check"):
            self.load()
        self.parents[0]["qualifier"] = "software-self-check"
        self.parents[0]["source"] = self.parents[1]["source"]
        with self.assertRaisesRegex(ValueError, "parent/source mismatch"):
            self.load()

    def test_source_escape_and_wrong_basename(self):
        for source in ("../outside.c", "/outside.c", "src/03_compute_instructions/../outside.c",
                       "src/03_compute_instructions/01_basic/not-the-parent.c",
                       "src\\03_compute_instructions\\01_basic\\file.c"):
            with self.subTest(source=source):
                self.rows[0]["source"] = source
                with self.assertRaisesRegex(ValueError, "escaping source"):
                    self.load()

    def test_symlink_source(self):
        path = self.root / self.parents[0]["source"]
        saved = path.with_suffix(".saved")
        path.rename(saved)
        path.symlink_to(saved.name)
        with self.assertRaisesRegex(ValueError, "symlink source"):
            self.load()

    def test_missing_number(self):
        self.rows.pop(1)
        with self.assertRaisesRegex(ValueError, "missing or out-of-range"):
            self.load()

    def test_duplicate_number_and_identifier(self):
        original = dict(self.rows[1])
        self.rows[1] = dict(self.rows[0])
        with self.assertRaisesRegex(ValueError, "duplicate subtest_id"):
            self.load()
        self.rows[1]["subtest_id"] += "_another"
        with self.assertRaisesRegex(ValueError, "duplicate subcase"):
            self.load()
        self.rows[1] = original

    def test_source_count_drift(self):
        path = self.root / self.parents[0]["source"]
        path.write_text("int main(void) { return progress_begin(__FILE__, 5U); }\n")
        with self.assertRaisesRegex(ValueError, "source count=5"):
            self.load()

    def test_missing_actual_progress_count(self):
        path = self.root / self.parents[0]["source"]
        path.write_text("// progress_begin(__FILE__, 4U);\nint main(void) { return 0; }\n")
        with self.assertRaisesRegex(ValueError, "one literal progress_begin"):
            self.load()

    def test_wrong_program_count(self):
        row = next(row for row in self.rows if row["parent_case_id"].endswith("008"))
        row["programs"] = "1"
        with self.assertRaisesRegex(ValueError, "wrong programs"):
            self.load()

    def test_invalid_subtest_name_or_noncanonical_number(self):
        for name in ("../../other", "HPU_IT_DIR_INS_C0_001__s00_foo/bar",
                     "HPU_IT_DIR_INS_C0_001__s00_含糊名", "HPU_IT_DIR_INS_C0_001__s01_variant"):
            with self.subTest(name=name):
                self.rows[0]["subtest_id"] = name
                with self.assertRaisesRegex(ValueError, "invalid subtest_id"):
                    self.load()
        self.rows[0]["subcase"] = "00"
        with self.assertRaisesRegex(ValueError, "invalid subcase"):
            self.load()

    def test_header_and_empty_field(self):
        self.write()
        path = self.root / "subtests/cases.tsv"
        path.write_text("not\tthe\theader\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "header"):
            SUPPORT.load_catalog(self.root)
        self.rows[0]["description"] = ""
        with self.assertRaisesRegex(ValueError, "missing or extra TSV field"):
            self.load()


class ElfTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hpu-subtest-elf-")
        self.addCleanup(self.temporary.cleanup)
        self.elf = Path(self.temporary.name) / "subtest.elf"
        parent = "HPU_IT_DIR_INS_C0_001"
        self.row = {"parent_case_id": parent, "subcase": "2",
                    "subtest_id": parent + "__s02_boundary", "programs": "1",
                    "description": "边界子项", "source": f"src/03_compute_instructions/01_basic/{parent}.c"}
        self.base = SUPPORT.ELF_ENTRY
        self.file_offset = 0x100
        self.argument_offset = 0x40
        self.source_offset = 0x100
        self.vector_offset = 0x400
        self.payload = bytearray(self.vector_offset + 2 * 16384)
        self.payload[:12] = b"\x13\0\0\0" * 3
        self.payload[self.argument_offset:self.argument_offset + 10] = b"subcase=2\0"
        identity = ("tests/hputest/" + self.row["source"]).encode() + b"\0"
        self.payload[self.source_offset:self.source_offset + len(identity)] = identity
        self.payload[self.vector_offset:self.vector_offset + 16384] = b"\x11" * 16384
        self.payload[self.vector_offset + 16384:] = b"\x22" * 16384
        self.symbols = [
            ("main", "T", self.base, 4),
            ("progress_begin", "T", self.base + 4, 4),
            ("subcase_selected", "T", self.base + 8, 4),
            ("__am_mainargs", "R", self.base + self.argument_offset, 0),
            ("RNS_A", "R", self.base + self.vector_offset, 16384),
            ("RNS_B", "R", self.base + self.vector_offset + 16384, 16384),
        ]
        self.write()
        self.mock_nm = patch.object(SUPPORT.subprocess, "run", side_effect=self.nm).start()
        self.addCleanup(patch.stopall)

    def write(self, machine=243, entry=None, flags=5):
        header = bytearray(64)
        header[:7] = b"\x7fELF\x02\x01\x01"
        struct.pack_into("<HHIQQQIHHHHHH", header, 16,
                         2, machine, 1, self.base if entry is None else entry,
                         64, 0, 0, 64, 56, 1, 0, 0, 0)
        ph = struct.pack("<IIQQQQQQ", 1, flags, self.file_offset, self.base, self.base,
                         len(self.payload), len(self.payload) + 0x1000, 0x100)
        self.elf.write_bytes(header + ph + b"\0" * (self.file_offset - 64 - len(ph)) + self.payload)

    def nm(self, arguments, **kwargs):
        self.assertEqual(arguments, ["test-riscv-nm", "-S", "--defined-only", "--format=posix", str(self.elf)])
        self.assertEqual(kwargs, {"check": True, "capture_output": True, "text": True})
        return SimpleNamespace(stdout="\n".join(
            f"{name} {kind} {address:x} {size:x}" if size else f"{name} {kind} {address:x} "
            for name, kind, address, size in self.symbols))

    def verify(self):
        SUPPORT.verify_elf(self.elf, self.row, "test-riscv-")

    def test_valid_size_less_mainargs_and_sized_vectors(self):
        self.verify()
        self.assertEqual(SUPPORT.read_symbol_bytes(self.elf, "__am_mainargs", "test-riscv-"), b"subcase=2\0")
        self.assertEqual(SUPPORT.read_symbol_bytes(self.elf, "RNS_A", "test-riscv-"), b"\x11" * 16384)

    def test_known_source_prefix(self):
        identity = ("nexus-am/tests/hputest/" + self.row["source"]).encode() + b"\0"
        self.payload[self.source_offset:self.source_offset + len(identity)] = identity
        self.write()
        self.verify()

    def test_all_or_wrong_selection_is_not_a_subtest(self):
        for value in (b"all\0", b"\0", b"subcase=1\0", b"subcase=2x\0", b"subcase=02\0"):
            with self.subTest(argument=value):
                self.payload[self.argument_offset:self.argument_offset + 64] = b"\0" * 64
                self.payload[self.argument_offset:self.argument_offset + len(value)] = value
                self.write()
                with self.assertRaisesRegex(ValueError, "wrong __am_mainargs"):
                    self.verify()

    def test_decoy_string_cannot_replace_mainargs(self):
        self.payload[self.argument_offset:self.argument_offset + 4] = b"all\0"
        self.payload[0x80:0x8a] = b"subcase=2\0"
        self.write()
        with self.assertRaisesRegex(ValueError, "wrong __am_mainargs"):
            self.verify()

    def test_mainargs_without_terminator_or_file_bytes(self):
        self.payload[self.argument_offset:self.argument_offset + 64] = b"x" * 64
        self.write()
        with self.assertRaisesRegex(ValueError, "not NUL terminated"):
            self.verify()
        name, kind, _address, size = self.symbols[3]
        self.symbols[3] = (name, kind, self.base + len(self.payload) + 1, size)
        with self.assertRaisesRegex(ValueError, "not uniquely file-backed"):
            self.verify()

    def test_wrong_parent_or_nonloaded_source_string(self):
        self.row["source"] = self.row["source"].replace("01_basic/", "02_other/")
        with self.assertRaisesRegex(ValueError, "wrong parent source identity"):
            self.verify()
        decoy = ("tests/hputest/" + self.row["source"]).encode() + b"\0"
        with self.elf.open("ab") as stream:
            stream.write(decoy)
        with self.assertRaisesRegex(ValueError, "wrong parent source identity"):
            self.verify()

    def test_not_a_basename_or_arbitrary_substring_match(self):
        identity = ("x-tests/hputest/" + self.row["source"]).encode() + b"\0"
        self.payload[self.source_offset:self.source_offset + len(identity)] = identity
        self.write()
        with self.assertRaisesRegex(ValueError, "wrong parent source identity"):
            self.verify()

    def test_missing_or_noncode_function(self):
        saved = self.symbols.pop(2)
        with self.assertRaisesRegex(ValueError, "missing ELF symbol: subcase_selected"):
            self.verify()
        self.symbols.append((saved[0], "R", saved[2], saved[3]))
        with self.assertRaisesRegex(ValueError, "real executable function"):
            self.verify()

    def test_missing_or_truncated_input(self):
        saved = self.symbols.pop()
        with self.assertRaisesRegex(ValueError, "missing ELF symbol: RNS_B"):
            self.verify()
        self.symbols.append((saved[0], saved[1], saved[2], 4))
        with self.assertRaisesRegex(ValueError, "4096-u32 input"):
            self.verify()

    def test_header_architecture_and_entry(self):
        self.write(machine=62)
        with self.assertRaisesRegex(ValueError, "RISC-V executable"):
            self.verify()
        self.write(entry=self.base + 4)
        with self.assertRaisesRegex(ValueError, "entry="):
            self.verify()
        self.write(flags=4)
        with self.assertRaisesRegex(ValueError, "not file-backed executable"):
            self.verify()
        self.elf.write_bytes(b"not ELF")
        with self.assertRaisesRegex(ValueError, "ELF64"):
            self.verify()

    def test_truncated_load_and_program_header(self):
        data = self.elf.read_bytes()
        self.elf.write_bytes(data[:-1])
        with self.assertRaisesRegex(ValueError, "truncated PT_LOAD"):
            self.verify()
        self.elf.write_bytes(data[:80])
        with self.assertRaisesRegex(ValueError, "truncated ELF program headers"):
            self.verify()

    def test_nm_tool_failure_is_actionable(self):
        self.mock_nm.side_effect = FileNotFoundError("missing cross nm")
        with self.assertRaisesRegex(ValueError, "cannot read ELF symbols"):
            self.verify()
        self.mock_nm.side_effect = subprocess.CalledProcessError(1, "nm")
        with self.assertRaisesRegex(ValueError, "cannot read ELF symbols"):
            self.verify()


if __name__ == "__main__":
    unittest.main()
