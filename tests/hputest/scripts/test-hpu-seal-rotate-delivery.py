#!/usr/bin/env python3
"""Regression gates for the HPU_SEAL BFV RotateRows package imported by AM."""

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
    "import_hpu_seal_rotate", Path(__file__).with_name("import-hpu-seal-rotate.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class HpuSealRotateDeliveryTests(unittest.TestCase):
    source = None
    encoder = None
    producer_commit = None

    @classmethod
    def setUpClass(cls):
        if cls.source is None or not (cls.source / "rotate.c").is_file():
            raise ValueError("pass --source pointing at a fresh HPU_SEAL Rotate package")
        if cls.encoder is None or not cls.encoder.is_file():
            raise ValueError("pass --encoder pointing at verify-ckks-encoding")

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
            return IMPORTER.prepare(self.source, self.producer_commit, self.encoder)

    def test_real_delivery_is_one_hpu_seal_bfv_rotate(self):
        prepared = self.prepare()
        self.assertEqual(len(prepared["program"]), 2593)
        self.assertEqual(len(prepared["encoded"]), 2593)
        self.assertEqual(prepared["metadata"]["dma_count"], 997)
        self.assertEqual(prepared["metadata"]["api"], IMPORTER.API)
        self.assertEqual(len(prepared["golden"]), 2 * 4 * 4096 * 4)

    def test_publish_freezes_runtime_layout_and_permissions(self):
        prepared = self.prepare()
        with tempfile.TemporaryDirectory(prefix="rotate-publish-") as root:
            destination = Path(root) / "rotate-data"
            IMPORTER.publish(self.source, destination, prepared, self.producer_commit)
            layout = (destination / "rotate_layout.h").read_text(encoding="utf-8")
            delivery = (destination / "rotate_delivery.h").read_text(encoding="utf-8")
            writable = (destination / "rotate_writable.u8.bin").read_bytes()
        self.assertIn("#define HPU_ROTATE_N 4096U", layout)
        self.assertIn("#define HPU_ROTATE_DMA_COUNT 997U", layout)
        self.assertIn('#include "rotate.h"', delivery)
        self.assertEqual(len(writable), 20162)
        self.assertFalse(any(writable[20098:]))

    def test_non_hpu_seal_api_is_rejected(self):
        path = self.source / "metadata.json"
        metadata = json.loads(path.read_text(encoding="utf-8"))
        metadata["api"] = "bare_auto"
        with self.assertRaisesRegex(ValueError, "api"):
            self.prepare(text={path: json.dumps(metadata)})

    def test_corrupted_golden_is_rejected(self):
        path = self.source / "golden.u32.bin"
        changed = bytearray(path.read_bytes())
        changed[16] ^= 1
        with self.assertRaisesRegex(ValueError, "golden hash"):
            self.prepare(binary={path: bytes(changed)})

    def test_non_rotated_slot_contract_is_rejected(self):
        path = self.source / "expected_slots.u64.bin"
        changed = bytearray(path.read_bytes())
        changed[0] ^= 1
        with self.assertRaisesRegex(ValueError, "left rotation"):
            self.prepare(binary={path: bytes(changed)})

    def test_dma_store_into_readonly_allocation_is_rejected(self):
        path = self.source / "resolved_dma.csv"
        schedule = IMPORTER.rows(path)
        first_store = next(row for row in schedule if row["direction"] == "dstore")
        first_store["allocation_id"] = "constants/modulus_table"
        first_store["line_offset"] = "0"
        first_store["line_count"] = "1"
        with self.assertRaisesRegex(ValueError, "read-only"):
            self.prepare(csv_rows={path: schedule})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--encoder", required=True, type=Path)
    parser.add_argument("--producer-commit", required=True)
    args, remaining = parser.parse_known_args()
    HpuSealRotateDeliveryTests.source = args.source.resolve(strict=True)
    HpuSealRotateDeliveryTests.encoder = args.encoder.resolve(strict=True)
    HpuSealRotateDeliveryTests.producer_commit = args.producer_commit
    unittest.main(argv=[sys.argv[0], *remaining])
