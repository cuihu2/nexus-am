#!/usr/bin/env python3
"""用当次 producer outputs 回归整体 NTT/INTT 接收检查，所有变异只存在于读取 mock。"""

import argparse
from contextlib import ExitStack
import copy
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "import_transform_data", Path(__file__).with_name("import-transform-data.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class TransformDeliveryTests(unittest.TestCase):
    source = None
    encodings = None

    @classmethod
    def setUpClass(cls):
        if cls.source is None:
            raise ValueError("pass --source pointing at this build's fresh producer outputs")
        if cls.encodings is None or not cls.encodings.is_file():
            raise ValueError("pass --encodings pointing at this build's upstream encoder_words.tsv")
        for direction in ("ntt", "intt"):
            if not (cls.source / direction / f"{direction}.c").is_file():
                raise ValueError(f"missing fresh {direction} package under {cls.source}")

    def validate(self, direction, *, binary=None, text=None, rows=None):
        """只截获目标文件读取；真实源文件不复制、不覆写、不删除。"""
        binary, text, rows = binary or {}, text or {}, rows or {}
        real_bytes, real_text, real_rows = Path.read_bytes, Path.read_text, IMPORTER.read_rows

        def read_bytes(path):
            return binary[path] if path in binary else real_bytes(path)

        def read_text(path, *args, **kwargs):
            return text[path] if path in text else real_text(path, *args, **kwargs)

        def read_rows(path):
            return copy.deepcopy(rows[path]) if path in rows else real_rows(path)

        with ExitStack() as stack:
            stack.enter_context(patch.object(Path, "read_bytes", read_bytes))
            stack.enter_context(patch.object(Path, "read_text", read_text))
            stack.enter_context(patch.object(IMPORTER, "read_rows", read_rows))
            return IMPORTER.validate_package(self.source, direction, self.encodings)

    def change_asset(self, direction, relative, index=17):
        """同时修改分文件及统一镜像，避免仅被同源字节一致性检查提前拦截。"""
        hardware = self.source / direction / "test_data/hardware"
        path = hardware / relative
        blob = bytearray(path.read_bytes())
        original = struct.unpack_from("<I", blob, index * 4)[0]
        changed = (original + 1) % IMPORTER.Q
        struct.pack_into("<I", blob, index * 4, changed)
        geometry = IMPORTER.one(IMPORTER.read_rows(hardware / "line_map.csv"), path=relative)
        image_path = hardware / "hpu_mem_image.u32.bin"
        image = bytearray(image_path.read_bytes())
        start = int(geometry["line_offset"]) * IMPORTER.LINE_BYTES
        image[start:start + len(blob)] = blob
        return {path: bytes(blob), image_path: bytes(image)}, changed

    def change_instruction(self, direction, instruction, changed_word):
        """同时修改所有交付载体，测试不会只依赖载体间一致就接受旧位段。"""
        package = self.source / direction
        source_path = package / f"{direction}.c"
        original_source = source_path.read_text(encoding="utf-8")
        emitted = iter(range(1000))

        def replace_word(match):
            return (f".word 0x{changed_word:08X}" if next(emitted) == instruction
                    else match.group(0))

        changed_source = re.sub(r"\.word 0x[0-9A-Fa-f]{8}", replace_word, original_source)
        words = [int(bits, 2) for bits in (package / f"{direction}.inst32").read_text().split()]
        words[instruction] = changed_word
        commands = [(word >> 7) | ((1 << 25) if word & 0x7F == 0x2B else 0) for word in words]
        text = {
            source_path: changed_source,
            package / f"{direction}.inst32": "".join(f"{word:032b}\n" for word in words),
            package / f"{direction}.cmd26": "".join(f"{word:026b}\n" for word in commands),
        }
        manifest = package / "dma_relocation_manifest.csv"
        relocations = IMPORTER.read_rows(manifest)
        for row in relocations:
            if int(row["instruction_index"]) == instruction:
                row["word_hex"] = f"0x{changed_word:08X}"
        return text, {manifest: relocations}

    def test_correct_ntt_and_intt_delivery(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                prepared = self.validate(direction)
                self.assertEqual(len(prepared["golden"]), 4096 * 4)
                self.assertEqual(len(prepared["image"]), IMPORTER.WINDOW_LINES * IMPORTER.LINE_BYTES)
                self.assertEqual(len(prepared["bindings"]), 16)
                self.assertEqual(prepared["mapped_words"][-1], 0x7000005B)
                self.assertTrue(all(word & 0x7F in (0x2B, 0x5B) for word in prepared["mapped_words"]))

    def test_one_golden_coefficient_changed_consistently_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                binary, changed = self.change_asset(direction, "images/expected.u32.bin")
                math_path = self.source / direction / "test_data/expected.bin"
                math_blob = bytearray(math_path.read_bytes())
                logical_index = (IMPORTER.physical_words(list(range(IMPORTER.N)),
                                  direction == "ntt"))[17]
                struct.pack_into("<Q", math_blob, logical_index * 8, changed)
                binary[math_path] = bytes(math_blob)
                with self.assertRaisesRegex(ValueError, "mathematical reference differs"):
                    self.validate(direction, binary=binary)

    def test_stage11_twiddle_changed_in_file_and_image_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                binary, _ = self.change_asset(
                    direction, f"constants/twiddle/{direction}/basis_00/stage_11.u32.bin")
                with self.assertRaisesRegex(ValueError, "stage 11 twiddle differs"):
                    self.validate(direction, binary=binary)

    def test_dma_manifest_register_mismatch_is_rejected(self):
        for direction in ("ntt", "intt"):
            for field, wrong in (("rs1", "x9"), ("rs2", "x12")):
                with self.subTest(direction=direction, field=field):
                    manifest = self.source / direction / "dma_relocation_manifest.csv"
                    relocations = IMPORTER.read_rows(manifest)
                    relocations[1][field] = wrong
                    with self.assertRaisesRegex(ValueError, "DMA relocation mismatch"):
                        self.validate(direction, rows={manifest: relocations})

    def test_dma_manifest_instruction_order_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                manifest = self.source / direction / "dma_relocation_manifest.csv"
                relocations = IMPORTER.read_rows(manifest)
                relocations[2], relocations[3] = relocations[3], relocations[2]
                with self.assertRaisesRegex(ValueError, "DMA relocation mismatch"):
                    self.validate(direction, rows={manifest: relocations})

    def test_c_register_binding_mismatch_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                source = self.source / direction / f"{direction}.c"
                changed = source.read_text().replace('__asm__("x10")', '__asm__("x9")', 1)
                with self.assertRaisesRegex(ValueError, "DMA source-register binding changed"):
                    self.validate(direction, text={source: changed})

    def test_wrong_parameter_modulus_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                params_path = self.source / direction / "test_data/params.json"
                params = json.loads(params_path.read_text())
                params["moduli"] = [IMPORTER.Q + 2]
                with self.assertRaisesRegex(ValueError, "expected N4096/Q0"):
                    self.validate(direction, text={params_path: json.dumps(params)})

    def test_wrong_modulus_record_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                binary, _ = self.change_asset(direction, "constants/mod_ctx.u32.bin", index=0)
                with self.assertRaisesRegex(ValueError, "mod context differs"):
                    self.validate(direction, binary=binary)

    def test_coherent_legacy_dma_word_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                # 两个程序的 index 2 均是 DLOAD p0；这是已退役的 rs1/rs2 高位布局。
                text, rows = self.change_instruction(direction, 2, 0x5A80012B)
                with self.assertRaises((ValueError, RuntimeError)):
                    self.validate(direction, text=text, rows=rows)

    def test_coherent_legacy_stage_word_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                program, _ = IMPORTER.expected_program(direction)
                instruction = program.index(f"p{direction} p0, p3, p1, 1, 0, 0")
                # 旧版把 twiddle p1 放在 [24:22]，不能用相互一致的 C/inst32/cmd26 放行。
                word = 0x4040040B if direction == "ntt" else 0x5040040B
                text, rows = self.change_instruction(direction, instruction, word)
                with self.assertRaises((ValueError, RuntimeError)):
                    self.validate(direction, text=text, rows=rows)

    def test_legacy_in_place_stage_is_rejected(self):
        for direction in ("ntt", "intt"):
            with self.subTest(direction=direction):
                source = self.source / direction / f"{direction}.c"
                changed = source.read_text().replace(
                    f"p{direction} p3, p0, p1, 0, 0, 0", f"p{direction} p0, p0, p1, 0, 0, 0")
                with self.assertRaisesRegex(ValueError, "producer program changed"):
                    self.validate(direction, text={source: changed})

    def test_inverse_loader_stage_direction_is_checked(self):
        manifest = self.source / "intt/test_data/hardware/twiddle_map.csv"
        rows = IMPORTER.read_rows(manifest)
        target = IMPORTER.one(rows, direction="intt", phase="butterfly", stage="0", basis_index="0")
        target["forward_stage"] = "0"
        with self.assertRaisesRegex(ValueError, "stage 0 metadata mismatch"):
            self.validate("intt", rows={manifest: rows})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path,
                        help="本次 fresh producer outputs，含 ntt/ 和 intt/")
    parser.add_argument("--encodings", required=True, type=Path,
                        help="本次 producer 重新编码的原始 encoder_words.tsv，不是映射后的表")
    args, remaining = parser.parse_known_args()
    TransformDeliveryTests.source = args.source.resolve(strict=True)
    TransformDeliveryTests.encodings = args.encodings.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
