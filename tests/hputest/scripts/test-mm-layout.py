#!/usr/bin/env python3
"""验证本轮 MM 数据使用 NTT 物理序；所有负例只 mock 读取，不改 producer 文件。"""

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
    "import_inline_asm_mm", Path(__file__).with_name("import-inline-asm-mm.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class MmLayoutTests(unittest.TestCase):
    source = None

    @classmethod
    def setUpClass(cls):
        if cls.source is None or not (cls.source / "test_data/params.json").is_file():
            raise ValueError("pass --source pointing at this build's fresh outputs/mm")

    def validate(self, *, binary=None, text=None, rows=None):
        binary, text, rows = binary or {}, text or {}, rows or {}
        real_bytes, real_text, real_rows = Path.read_bytes, Path.read_text, IMPORTER.read_csv

        def read_bytes(path):
            return binary[path] if path in binary else real_bytes(path)

        def read_text(path, *args, **kwargs):
            return text[path] if path in text else real_text(path, *args, **kwargs)

        def read_rows(path):
            return copy.deepcopy(rows[path]) if path in rows else real_rows(path)

        with ExitStack() as stack:
            stack.enter_context(patch.object(Path, "read_bytes", read_bytes))
            stack.enter_context(patch.object(Path, "read_text", read_text))
            stack.enter_context(patch.object(IMPORTER, "read_csv", read_rows))
            return IMPORTER.validate_data(self.source)

    def test_fresh_ntt_domain_package_passes(self):
        count, modulus, selected = self.validate()
        self.assertEqual((count, modulus), (4096, 50061313))
        self.assertEqual(set(selected), set(IMPORTER.EXPECTED_IMAGES))

    def test_coherent_natural_order_vectors_and_manifest_are_rejected(self):
        data = self.source / "test_data"
        hardware = data / "hardware"
        manifest_path = hardware / "hardware_manifest.csv"
        manifest = IMPORTER.read_csv(manifest_path)
        binary = {}
        logical = {}
        for name in ("input_a", "input_b", "expected"):
            logical[name] = struct.unpack("<4096Q", (data / f"{name}.bin").read_bytes())
            natural_u32 = struct.pack("<4096I", *logical[name])
            relative = f"images/{name}.u32.bin"
            path = hardware / relative
            self.assertNotEqual(natural_u32, path.read_bytes(),
                                f"{name} fixture must distinguish natural and NTT physical order")
            binary[path] = natural_u32
            for row in manifest:
                if row["path"] == relative:
                    row["image_fnv1a64"] = f"0x{IMPORTER.fnv1a64(natural_u32):016x}"
        # 三张表一起错误地变为自然序后，逐项乘法仍正确，现有 manifest 也与文件一致。
        # 门禁必须依据独立的物理/逻辑映射拒绝，不能只检查 A*B==golden。
        self.assertTrue(all((a * b) % 50061313 == golden for a, b, golden in
                            zip(logical["input_a"], logical["input_b"], logical["expected"])))
        with self.assertRaisesRegex(RuntimeError, "MM input_a: physical/logical mapping mismatch"):
            self.validate(binary=binary, rows={manifest_path: manifest})

    def test_coefficient_domain_declaration_is_rejected(self):
        path = self.source / "test_data/params.json"
        params = json.loads(path.read_text())
        params["input_domain"] = "coefficient"
        params["output_domain"] = "coefficient"
        with self.assertRaisesRegex(RuntimeError, "must declare the current P-network NTT layout"):
            self.validate(text={path: json.dumps(params)})

    def test_changed_hardware_word_is_rejected_by_existing_manifest_check(self):
        path = self.source / "test_data/hardware/images/input_b.u32.bin"
        blob = bytearray(path.read_bytes())
        value = struct.unpack_from("<I", blob, 17 * 4)[0]
        struct.pack_into("<I", blob, 17 * 4, (value + 1) % 50061313)
        with self.assertRaisesRegex(RuntimeError, "FNV mismatch for images/input_b.u32.bin"):
            self.validate(binary={path: bytes(blob)})

    def test_truncated_natural_mathematical_input_is_rejected(self):
        path = self.source / "test_data/input_a.bin"
        with self.assertRaisesRegex(RuntimeError, "MM input_a: expected 4096 natural-order uint64 values"):
            self.validate(binary={path: path.read_bytes()[:-8]})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path,
                        help="本轮 fresh producer 的 outputs/mm 目录")
    args, remaining = parser.parse_known_args()
    MmLayoutTests.source = args.source.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
