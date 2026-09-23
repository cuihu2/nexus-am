#!/usr/bin/env python3
"""用同批真实 KeySwitch/Auto 产物回归 AM KeySwitch 语义导入门禁。"""

import argparse
from contextlib import ExitStack
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "import_keyswitch_data", Path(__file__).with_name("import-keyswitch-data.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class KeySwitchDeliveryTests(unittest.TestCase):
    source = None
    encodings = None

    @classmethod
    def setUpClass(cls):
        if cls.source is None or not (cls.source / "keyswitch/keyswitch.c").is_file():
            raise ValueError("pass --source pointing at fresh producer outputs")
        if cls.encodings is None or not cls.encodings.is_file():
            raise ValueError("pass --encodings pointing at fresh encoder_words.tsv")

    def prepare(self, *, binary=None, text=None, csv_rows=None):
        binary, text, csv_rows = binary or {}, text or {}, csv_rows or {}
        real_bytes, real_text, real_rows = Path.read_bytes, Path.read_text, IMPORTER.rows

        def read_bytes(path):
            return binary[path] if path in binary else real_bytes(path)

        def read_text(path, *args, **kwargs):
            return text[path] if path in text else real_text(path, *args, **kwargs)

        def read_rows(path):
            return copy.deepcopy(csv_rows[path]) if path in csv_rows else real_rows(path)

        with ExitStack() as stack:
            stack.enter_context(patch.object(Path, "read_bytes", read_bytes))
            stack.enter_context(patch.object(Path, "read_text", read_text))
            stack.enter_context(patch.object(IMPORTER, "rows", read_rows))
            return IMPORTER.prepare(self.source, self.encodings)

    def test_real_delivery_builds_complete_independent_am_window(self):
        prepared = self.prepare()
        self.assertEqual(len(prepared["program"]), 2167)
        self.assertEqual(len(prepared["resolved"]), 716)
        self.assertEqual(prepared["window_lines"], 14657)
        self.assertEqual(len(prepared["golden"]), 2 * 4 * 4096 * 4)
        self.assertEqual(len(prepared["window"]), (14657 + 64) * 256)
        output = prepared["output_offset"] * 256
        self.assertEqual(prepared["window"][output:output + 4], struct.pack("<I", 0xDEADBEEF))
        self.assertTrue(all(row["status"] == "RESOLVED" for row in prepared["resolved"]))
        stores = {row["artifact"] for row in prepared["resolved"]
                  if row["direction"] == "dstore"}
        self.assertEqual(stores, {"am/runtime/keyswitch_scratch.u32.bin",
                                  "am/output_q.u32.bin"})

    def test_published_header_distinguishes_active_window_from_guard(self):
        with __import__("tempfile").TemporaryDirectory(prefix="keyswitch-publish-") as root:
            destination = Path(root) / "keyswitch-data"
            IMPORTER.publish(destination, self.prepare(), "1" * 40, self.encodings)
            header = (destination / "keyswitch_delivery.h").read_text(encoding="utf-8")
        self.assertIn("#define HPU_KEYSWITCH_WINDOW_LINES 14657U", header)
        self.assertIn("#define HPU_KEYSWITCH_GUARD_OFFSET 14657U", header)
        self.assertIn("#define HPU_KEYSWITCH_TOTAL_LINES 14721U", header)

    def test_changed_key_parameter_is_rejected(self):
        path = self.source / "keyswitch/test_data/params.json"
        params = json.loads(path.read_text(encoding="utf-8"))
        params["N"] = 2048
        with self.assertRaisesRegex(ValueError, "expected fixed N4096"):
            self.prepare(text={path: json.dumps(params)})

    def test_changed_hardware_blob_is_rejected_by_manifest_checksum(self):
        path = self.source / "keyswitch/test_data/hardware/images/input_t2_q.u32.bin"
        changed = bytearray(path.read_bytes())
        changed[17] ^= 1
        with self.assertRaisesRegex(ValueError, "hardware checksum mismatch"):
            self.prepare(binary={path: bytes(changed)})

    def test_key_line_map_overlap_is_rejected(self):
        path = self.source / "keyswitch/test_data/hardware/line_map.csv"
        entries = IMPORTER.rows(path)
        entries[1]["line_offset"] = entries[0]["line_offset"]
        with self.assertRaisesRegex(ValueError, "line/hardware manifest mismatch"):
            self.prepare(csv_rows={path: entries})

    def test_auto_witness_cannot_change_a_keyswitch_dma_binding(self):
        path = self.source / "auto/test_data/dma_plan.csv"
        plan = IMPORTER.rows(path)
        witness = len(plan) - IMPORTER.EXPECTED_DMA
        plan[witness]["object_slot"] = "7"
        with self.assertRaisesRegex(ValueError, "unresolved or mismatched DMA row"):
            self.prepare(csv_rows={path: plan})

    def test_encoded_relocation_must_match_independent_am_plan(self):
        path = self.source / "keyswitch/dma_relocation_manifest.csv"
        relocations = IMPORTER.rows(path)
        relocations[0]["obj_id"] = "7"
        with self.assertRaisesRegex(ValueError, "relocation 0 differs from encoded program"):
            self.prepare(csv_rows={path: relocations})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--encodings", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    KeySwitchDeliveryTests.source = args.source.resolve(strict=True)
    KeySwitchDeliveryTests.encodings = args.encodings.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
