#!/usr/bin/env python3
"""用临时 producer 包回归 KeySwitch 原始数据暂存，不依赖交叉编译器。"""

import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("stage-keyswitch-package.py")
SPEC = importlib.util.spec_from_file_location("stage_keyswitch_package", SCRIPT)
STAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGER)
COMMIT = "1" * 40


class KeySwitchPackageStagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hpu-keyswitch-stage-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "producer/outputs/keyswitch"
        self.destination = self.root / "generated/keyswitch-source"
        self.make_package()

    @staticmethod
    def write_csv(path, fields, records):
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(records)

    def make_package(self):
        for relative in STAGER.REQUIRED_FILES:
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n", encoding="utf-8")

        data = self.source / "test_data"
        hardware = data / "hardware"
        (data / "params.json").write_text(
            json.dumps({"operation": "keyswitch"}), encoding="utf-8")
        (hardware / "hpu_mem_config.json").write_text(
            json.dumps({"size_lines": 2}), encoding="utf-8")
        (hardware / "hpu_mem_image.u32.bin").write_bytes(bytes(2 * STAGER.LINE_BYTES))

        plan_fields = ["instruction_index", "dma_index", "direction", "object_slot",
                       "logical_object", "artifact", "line_offset", "line_count", "status"]
        plan = [
            {"instruction_index": "0", "dma_index": "0", "direction": "dload",
             "object_slot": "0", "logical_object": "base", "artifact": "images/base.u32.bin",
             "line_offset": "0", "line_count": "1", "status": "RESOLVED"},
            {"instruction_index": "1", "dma_index": "1", "direction": "dstore",
             "object_slot": "1", "logical_object": "output", "artifact": "images/out.u32.bin",
             "line_offset": "1", "line_count": "1", "status": "RESOLVED"},
        ]
        self.write_csv(data / "dma_plan.csv", plan_fields, plan)
        relocation_fields = ["instruction_index", "dma_index", "direction", "obj_id",
                             "type_or_release", "flag", "rs1", "rs2", "word_hex", "normalized_asm"]
        relocations = [
            {"instruction_index": row["instruction_index"], "dma_index": row["dma_index"],
             "direction": row["direction"], "obj_id": row["object_slot"],
             "type_or_release": "0", "flag": "0", "rs1": "x10", "rs2": "x11",
             "word_hex": "0x0000002b", "normalized_asm": row["direction"]}
            for row in plan
        ]
        self.write_csv(self.source / "dma_relocation_manifest.csv",
                       relocation_fields, relocations)

        (data / "fixture.bin").write_bytes(b"math")
        self.write_csv(data / "artifact_manifest.csv",
                       ["path", "readable_path", "role", "shape", "elements", "bytes",
                        "hardware_visible", "fnv1a64"],
                       [{"path": "fixture.bin", "readable_path": "", "role": "fixture",
                         "shape": "1", "elements": "1", "bytes": "8",
                         "hardware_visible": "1", "fnv1a64": "0x1"}])
        self.write_csv(hardware / "hardware_manifest.csv",
                       ["path", "readable_path", "role", "shape", "payload_words",
                        "padded_words", "line_offset", "line_count", "payload_fnv1a64",
                        "image_fnv1a64"],
                       [{"path": "hpu_mem_image.u32.bin", "readable_path": "",
                         "role": "complete image", "shape": "", "payload_words": "128",
                         "padded_words": "128", "line_offset": "0", "line_count": "2",
                         "payload_fnv1a64": "0x1", "image_fnv1a64": "0x1"}])
        self.write_csv(hardware / "line_map.csv", ["path", "line_offset", "line_count"],
                       [{"path": "hpu_mem_image.u32.bin", "line_offset": "0", "line_count": "2"}])

    def test_valid_package_is_staged_with_explicit_nonsemantic_marker(self):
        summary = STAGER.validate(self.source)
        STAGER.publish(self.source, self.destination, COMMIT, summary)
        self.assertEqual((self.destination / "producer_commit.txt").read_text(), COMMIT + "\n")
        marker = json.loads((self.destination / "STAGING_STATUS.json").read_text())
        self.assertFalse(marker["semantic_import"])
        self.assertEqual(marker["validation_scope"], "producer-package-structure-only")
        self.assertEqual(marker["dma_rows"], 2)
        self.assertTrue((self.destination / "upstream/keyswitch.c").is_file())

    def test_unresolved_dma_is_rejected_before_publish(self):
        path = self.source / "test_data/dma_plan.csv"
        records = STAGER.rows(path)
        records[1]["status"] = "UNRESOLVED"
        self.write_csv(path, list(records[0]), records)
        with self.assertRaisesRegex(ValueError, "span is not RESOLVED"):
            STAGER.validate(self.source)
        self.assertFalse(self.destination.exists())

    def test_wrong_memory_image_size_is_rejected(self):
        (self.source / "test_data/hardware/hpu_mem_image.u32.bin").write_bytes(b"short")
        with self.assertRaisesRegex(ValueError, "image size does not match"):
            STAGER.validate(self.source)

    def test_publish_replaces_only_the_owned_staging_directory(self):
        summary = STAGER.validate(self.source)
        STAGER.publish(self.source, self.destination, COMMIT, summary)
        (self.destination / "stale.txt").write_text("stale", encoding="utf-8")
        STAGER.publish(self.source, self.destination, COMMIT, summary)
        self.assertFalse((self.destination / "stale.txt").exists())
        self.assertTrue((self.destination / "upstream/keyswitch.c").is_file())

    def test_publish_refuses_to_replace_an_unowned_directory(self):
        self.destination.mkdir(parents=True)
        (self.destination / "user-file.txt").write_text("keep", encoding="utf-8")
        summary = STAGER.validate(self.source)
        with self.assertRaisesRegex(ValueError, "refusing to replace an unowned"):
            STAGER.publish(self.source, self.destination, COMMIT, summary)
        self.assertEqual((self.destination / "user-file.txt").read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
