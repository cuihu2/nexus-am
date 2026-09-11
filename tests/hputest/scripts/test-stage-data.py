#!/usr/bin/env python3
"""单 stage 表导入正例与物理 ABI 负例；变异只在读取 mock 内。"""

import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "stage_importer", Path(__file__).with_name("import-stage-data.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class StageDataTests(unittest.TestCase):
    source = None

    def test_six_current_producer_tables(self):
        selection, files = IMPORTER.validate(self.source)
        self.assertEqual(len(selection), 6)
        self.assertEqual(len(files), 6)
        self.assertTrue(all(len(raw) == 8192 for _, raw in files))

    def test_old_convention_rejected(self):
        target = self.source / "ntt/test_data/hardware/abi.json"
        metadata = json.loads(target.read_text())
        metadata["twiddle"]["convention"] = "group-major radix-2 DIT with one N/2-word image per stage"
        real = Path.read_text
        with patch.object(Path, "read_text", lambda path, **kwargs:
                          json.dumps(metadata) if path == target else real(path, **kwargs)):
            with self.assertRaisesRegex(ValueError, "unsupported stage twiddle convention"):
                IMPORTER.validate(self.source)

    def test_corrupt_inverse_table_and_unified_image_rejected(self):
        hardware = self.source / "ntt/test_data/hardware"
        name = "constants/twiddle/intt/basis_00/stage_01.u32.bin"
        target, image_path = hardware / name, hardware / "hpu_mem_image.u32.bin"
        table = bytearray(target.read_bytes())
        value = struct.unpack_from("<I", table, 17 * 4)[0]
        struct.pack_into("<I", table, 17 * 4, (value + 1) % IMPORTER.Q)
        image = bytearray(image_path.read_bytes())
        geometry = IMPORTER.unique_row(IMPORTER.rows(hardware / "line_map.csv"), path=name)
        offset = int(geometry["line_offset"]) * IMPORTER.LINE_BYTES
        image[offset:offset + len(table)] = table
        replacements = {target: bytes(table), image_path: bytes(image)}
        real = Path.read_bytes
        with patch.object(Path, "read_bytes", lambda path:
                          replacements[path] if path in replacements else real(path)):
            with self.assertRaisesRegex(ValueError, "intt/stage1: word=17"):
                IMPORTER.validate(self.source)

    def test_inverse_forward_stage_is_not_instruction_stage(self):
        target = self.source / "ntt/test_data/hardware/twiddle_map.csv"
        changed = IMPORTER.rows(target)
        row = IMPORTER.unique_row(changed, direction="intt", basis_index="0", phase="butterfly", stage="0")
        row["forward_stage"] = "0"
        real = IMPORTER.rows
        with patch.object(IMPORTER, "rows", lambda path: changed if path == target else real(path)):
            with self.assertRaisesRegex(ValueError, "wrong forward_stage"):
                IMPORTER.validate(self.source)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    StageDataTests.source = args.source.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
