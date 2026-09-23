#!/usr/bin/env python3
"""Regression gates for the HPU_SEAL BFV HADD package imported by AM."""

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
    "import_hpu_seal_hadd", Path(__file__).with_name("import-hpu-seal-hadd.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class HpuSealHaddDeliveryTests(unittest.TestCase):
    source = None
    encodings = None
    producer_commit = None

    @classmethod
    def setUpClass(cls):
        if cls.source is None or not (cls.source / "hadd.c").is_file():
            raise ValueError("pass --source pointing at a fresh HPU_SEAL HADD package")
        if cls.encodings is None or not cls.encodings.is_file():
            raise ValueError("pass --encodings pointing at encoder_words.tsv")

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
            return IMPORTER.prepare(self.source, self.producer_commit, self.encodings)

    def test_real_delivery_is_one_hpu_seal_bfv_add(self):
        prepared = self.prepare()
        self.assertEqual(len(prepared["program"]), 59)
        self.assertEqual(len(prepared["encoded"]), 59)
        self.assertEqual(prepared["metadata"]["dma_count"], 25)
        self.assertEqual(prepared["metadata"]["api"], IMPORTER.API)
        self.assertEqual(len(prepared["golden"]), 2 * 4 * 4096 * 4)

    def test_publish_freezes_runtime_layout(self):
        prepared = self.prepare()
        with tempfile.TemporaryDirectory(prefix="hadd-publish-") as root:
            destination = Path(root) / "hadd-data"
            IMPORTER.publish(self.source, destination, prepared, self.producer_commit)
            layout = (destination / "hadd_layout.h").read_text(encoding="utf-8")
            delivery = (destination / "hadd_delivery.h").read_text(encoding="utf-8")
        self.assertIn("#define HPU_HADD_N 4096U", layout)
        self.assertIn("#define HPU_HADD_DMA_COUNT 25U", layout)
        self.assertIn('#include "hadd.h"', delivery)

    def test_non_hpu_seal_api_is_rejected(self):
        path = self.source / "metadata.json"
        metadata = json.loads(path.read_text(encoding="utf-8"))
        metadata["api"] = "handwritten_padd"
        with self.assertRaisesRegex(ValueError, "api"):
            self.prepare(text={path: json.dumps(metadata)})

    def test_corrupted_golden_is_rejected(self):
        path = self.source / "golden.u32.bin"
        changed = bytearray(path.read_bytes())
        changed[16] ^= 1
        with self.assertRaisesRegex(ValueError, "golden differs"):
            self.prepare(binary={path: bytes(changed)})

    def test_dma_outside_named_allocation_is_rejected(self):
        path = self.source / "resolved_dma.csv"
        schedule = IMPORTER.rows(path)
        schedule[1]["line_offset"] = "999999"
        with self.assertRaisesRegex(ValueError, "differs from its allocation"):
            self.prepare(csv_rows={path: schedule})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--encodings", required=True, type=Path)
    parser.add_argument("--producer-commit", required=True)
    args, remaining = parser.parse_known_args()
    HpuSealHaddDeliveryTests.source = args.source.resolve(strict=True)
    HpuSealHaddDeliveryTests.encodings = args.encodings.resolve(strict=True)
    HpuSealHaddDeliveryTests.producer_commit = args.producer_commit
    unittest.main(argv=[sys.argv[0], *remaining])
