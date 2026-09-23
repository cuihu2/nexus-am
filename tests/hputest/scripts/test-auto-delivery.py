#!/usr/bin/env python3
"""Regression gates for the producer NTT+Auto delivery imported by AM."""

import argparse
from contextlib import ExitStack
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "import_auto_data", Path(__file__).with_name("import-auto-data.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class AutoDeliveryTests(unittest.TestCase):
    source = None
    encodings = None

    @classmethod
    def setUpClass(cls):
        if cls.source is None or not (cls.source / "auto/auto.c").is_file():
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

    def test_real_delivery_uses_complete_producer_plan(self):
        prepared = self.prepare()
        self.assertEqual(len(prepared["program"]), 3009)
        self.assertEqual(len(prepared["resolved"]), 941)
        self.assertEqual(len(prepared["golden"]), 2 * 4 * 4096 * 4)
        self.assertEqual(len(prepared["window"]),
                         (prepared["window_lines"] + 64) * 256)
        self.assertEqual(prepared["output_offset"] + 8 * 64,
                         prepared["workspace_offset"] + prepared["workspace_lines"])
        self.assertTrue(all(row["status"] == "RESOLVED"
                            for row in prepared["resolved"]))

    def test_publish_freezes_window_workspace_output_and_guard(self):
        prepared = self.prepare()
        with tempfile.TemporaryDirectory(prefix="auto-publish-") as root:
            destination = Path(root) / "auto-data"
            IMPORTER.publish(destination, prepared, "1" * 40, self.encodings)
            layout = (destination / "auto_layout.h").read_text(encoding="utf-8")
            delivery = (destination / "auto_delivery.h").read_text(encoding="utf-8")
        self.assertIn(f"#define HPU_AUTO_WINDOW_LINES {prepared['window_lines']}U", layout)
        self.assertIn(f"#define HPU_AUTO_OUTPUT_OFFSET {prepared['output_offset']}U", layout)
        self.assertIn("#define HPU_AUTO_GUARD_LINES 64U", layout)
        self.assertIn('#include "auto_layout.h"', delivery)

    def test_host_preprocessed_auto_is_rejected(self):
        path = self.source / "auto/test_data/AUTO_LAYOUT.json"
        layout = json.loads(path.read_text(encoding="utf-8"))
        layout["host_preprocess"] = True
        with self.assertRaisesRegex(ValueError, "unsupported Galois"):
            self.prepare(text={path: json.dumps(layout)})

    def test_unresolved_dma_is_rejected(self):
        path = self.source / "auto/test_data/dma_plan.csv"
        plan = IMPORTER.rows(path)
        plan[17]["status"] = "UNRESOLVED"
        with self.assertRaisesRegex(ValueError, "unresolved or mismatched DMA"):
            self.prepare(csv_rows={path: plan})

    def test_dma_span_cannot_escape_its_artifact(self):
        path = self.source / "auto/test_data/dma_plan.csv"
        plan = IMPORTER.rows(path)
        plan[17]["line_count"] = "999999"
        with self.assertRaisesRegex(ValueError, "exceeds its artifact"):
            self.prepare(csv_rows={path: plan})

    def test_golden_checksum_is_enforced(self):
        path = self.source / "auto/test_data/expected/ciphertext_q.bin"
        changed = bytearray(path.read_bytes())
        changed[11] ^= 1
        with self.assertRaisesRegex(ValueError, "golden manifest/checksum mismatch"):
            self.prepare(binary={path: bytes(changed)})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--encodings", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    AutoDeliveryTests.source = args.source.resolve(strict=True)
    AutoDeliveryTests.encodings = args.encodings.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
