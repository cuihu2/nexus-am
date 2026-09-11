#!/usr/bin/env python3
"""用当次 producer 数据验证 BConv 接收门禁；变异仅在读取 mock 中存在。"""

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
    "import_bconv_data", Path(__file__).with_name("import-bconv-data.py"))
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class BconvDeliveryTests(unittest.TestCase):
    source = None
    encodings = None

    @classmethod
    def setUpClass(cls):
        if cls.source is None or not (cls.source / "bconv/bconv.c").is_file():
            raise ValueError("pass --source pointing at this build's fresh producer outputs")
        if cls.encodings is None or not cls.encodings.is_file():
            raise ValueError("pass --encodings pointing at this build's fresh encoder_words.tsv")

    def validate(self, *, binary=None, text=None, rows=None):
        """只拦截必要读取；不复制、覆写或删除交付目录。"""
        binary, text, rows = binary or {}, text or {}, rows or {}
        real_bytes, real_text, real_rows = Path.read_bytes, Path.read_text, IMPORTER.rows

        def read_bytes(path):
            return binary[path] if path in binary else real_bytes(path)

        def read_text(path, *args, **kwargs):
            return text[path] if path in text else real_text(path, *args, **kwargs)

        def read_rows(path):
            return copy.deepcopy(rows[path]) if path in rows else real_rows(path)

        with ExitStack() as stack:
            stack.enter_context(patch.object(Path, "read_bytes", read_bytes))
            stack.enter_context(patch.object(Path, "read_text", read_text))
            stack.enter_context(patch.object(IMPORTER, "rows", read_rows))
            return IMPORTER.validate(self.source, self.encodings)

    def change_instruction(self, instruction, changed_word):
        """同步篡改 C、inst32、cmd26 和 relocation，不能只靠载体互相一致放行。"""
        package = self.source / "bconv"
        c_path = package / "bconv.c"
        original_c = c_path.read_text(encoding="utf-8")
        index = iter(range(1000))

        def replace_word(match):
            return f".word 0x{changed_word:08X}" if next(index) == instruction else match.group(0)

        changed_c = re.sub(r"\.word 0x[0-9A-Fa-f]{8}", replace_word, original_c)
        words = [int(bits, 2) for bits in (package / "bconv.inst32").read_text().split()]
        words[instruction] = changed_word
        commands = [(word >> 7) | ((1 << 25) if word & 0x7F == 0x2B else 0) for word in words]
        changed_text = {
            c_path: changed_c,
            package / "bconv.inst32": "".join(f"{word:032b}\n" for word in words),
            package / "bconv.cmd26": "".join(f"{word:026b}\n" for word in commands),
        }
        manifest = package / "dma_relocation_manifest.csv"
        changed_rows = IMPORTER.rows(manifest)
        for row in changed_rows:
            if int(row["instruction_index"]) == instruction:
                row["word_hex"] = f"0x{changed_word:08X}"
        return changed_text, {manifest: changed_rows}

    def test_correct_delivery_preserves_full_program_and_poisoned_outputs(self):
        prepared = self.validate()
        self.assertEqual(len(prepared["plan"]), 40)
        self.assertEqual(len(prepared["words"]), 93)
        self.assertEqual(len(prepared["golden"]), 3 * 4096 * 4)
        self.assertEqual(len(prepared["normalized"]), 4 * 4096 * 4)
        self.assertEqual(len(prepared["window"]), 2048 * 256)
        self.assertEqual(prepared["words"][-1], 0x7000005B)
        self.assertEqual(sum(word == 0x7000005B for word in prepared["words"]), 1)
        writable = prepared["window"][1280 * 256:1728 * 256]
        self.assertEqual(writable, struct.pack("<I", 0xDEADBEEF) * ((1728 - 1280) * 64))

    def test_changed_p_golden_coefficient_is_rejected_by_fast_bconv_reference(self):
        path = self.source / "bconv/test_data/expected_p.bin"
        blob = bytearray(path.read_bytes())
        coefficient = 2 * 4096 + 17  # 第三个 P 分量，避免只检查 P0 的假门禁。
        value = struct.unpack_from("<Q", blob, coefficient * 8)[0]
        struct.pack_into("<Q", blob, coefficient * 8, (value + 1) % IMPORTER.MODULI[6])
        with self.assertRaisesRegex(ValueError, "FastBConv producer golden mismatch: target=2 coefficient=17"):
            self.validate(binary={path: bytes(blob)})

    def test_each_output_basis_is_mapped_to_physical_coefficient_order(self):
        prepared = self.validate()
        golden = struct.unpack(f"<{3 * IMPORTER.N}I", prepared["golden"])
        normalized = struct.unpack(f"<{4 * IMPORTER.N}I", prepared["normalized"])
        data = self.source / "bconv/test_data"
        logical_golden = struct.unpack(f"<{3 * IMPORTER.N}Q", (data / "expected_p.bin").read_bytes())
        logical_inputs = struct.unpack(f"<{4 * IMPORTER.N}Q", (data / "input_q.bin").read_bytes())
        # 独立迭代更新 reverse index，不调用被测 importer 的映射函数。
        for basis in range(4):
            modulus = IMPORTER.MODULI[basis]
            hat = 1
            for other in range(4):
                if other != basis:
                    hat *= IMPORTER.MODULI[other]
            inverse = pow(hat % modulus, -1, modulus)
            reverse = 0
            for physical in range(IMPORTER.N):
                self.assertEqual(normalized[basis * IMPORTER.N + physical],
                                 logical_inputs[basis * IMPORTER.N + reverse] * inverse % modulus)
                if basis < 3:
                    self.assertEqual(golden[basis * IMPORTER.N + physical],
                                     logical_golden[basis * IMPORTER.N + reverse])
                bit = IMPORTER.N >> 1
                while bit != 0 and reverse & bit:
                    reverse ^= bit
                    bit >>= 1
                reverse ^= bit

    def test_coherent_natural_order_input_and_image_are_rejected(self):
        data = self.source / "bconv/test_data"
        hardware = data / "hardware"
        logical = struct.unpack(f"<{4 * IMPORTER.N}Q", (data / "input_q.bin").read_bytes())
        natural = struct.pack(f"<{len(logical)}I", *logical)
        input_path = hardware / "images/input_q.u32.bin"
        self.assertNotEqual(natural, input_path.read_bytes(), "fixture must distinguish physical order")
        image_path = hardware / "hpu_mem_image.u32.bin"
        image = bytearray(image_path.read_bytes())
        image[:len(natural)] = natural
        with self.assertRaisesRegex(ValueError, "per-basis bit-reversed mathematical input"):
            self.validate(binary={input_path: natural, image_path: bytes(image)})

    def test_obsolete_natural_order_metadata_is_rejected(self):
        path = self.source / "bconv/test_data/params.json"
        params = json.loads(path.read_text(encoding="utf-8"))
        params["hardware_layout"] = "hardware/: little-endian uint32 in natural polynomial order"
        with self.assertRaisesRegex(ValueError, "expected bit-reversed coefficient-domain hardware layout"):
            self.validate(text={path: json.dumps(params)})

    def test_changed_normalization_constant_in_file_and_unified_image_is_rejected(self):
        hardware = self.source / "bconv/test_data/hardware"
        relative = "images/constants/qhat_inv_q.u32.bin"
        path = hardware / relative
        blob = bytearray(path.read_bytes())
        index = 4096 + 31  # Q1 的某一项；即使分文件与统一镜像一致也必须拒绝。
        original = struct.unpack_from("<I", blob, index * 4)[0]
        struct.pack_into("<I", blob, index * 4, (original + 1) % IMPORTER.MODULI[1])
        image_path = hardware / "hpu_mem_image.u32.bin"
        image = bytearray(image_path.read_bytes())
        start = IMPORTER.GEOMETRY[relative][0] * IMPORTER.LINE_BYTES
        image[start:start + len(blob)] = blob
        with self.assertRaisesRegex(ValueError, "Q1: source-hat inverse mismatch"):
            self.validate(binary={path: bytes(blob), image_path: bytes(image)})

    def test_dma_to_another_legal_input_basis_is_rejected(self):
        path = self.source / "bconv/test_data/dma_plan.csv"
        plan = IMPORTER.rows(path)
        # DMA1本应读取Q0；改成Q1仍在相同input artifact的合法范围，不能按越界检查放行。
        self.assertEqual(plan[1]["artifact"], "images/input_q.u32.bin")
        plan[1]["line_offset"] = "64"
        plan[1]["logical_object"] = "bconv.input_q[1]"
        with self.assertRaisesRegex(ValueError, "DMA 1: logical source/target basis binding mismatch"):
            self.validate(rows={path: plan})

    def test_dstore_to_readonly_input_is_rejected(self):
        path = self.source / "bconv/test_data/dma_plan.csv"
        plan = IMPORTER.rows(path)
        self.assertEqual(plan[3]["direction"], "dstore")
        plan[3]["artifact"] = "images/input_q.u32.bin"
        plan[3]["line_offset"] = "0"
        plan[3]["logical_object"] = "bconv.input_q[0]"
        with self.assertRaisesRegex(ValueError, "DMA 3: logical source/target basis binding mismatch"):
            self.validate(rows={path: plan})

    def test_coherent_retired_encoding_is_rejected_against_fresh_encoder(self):
        # 同时改全部相关载体：旧DMA两种布局、错误计算主opcode，都须被fresh编码表拦截。
        for instruction, word in ((2, 0x00B5102B), (2, 0x5A80012B), (92, 0x7000007B)):
            with self.subTest(instruction=instruction, word=f"0x{word:08X}"):
                text, rows = self.change_instruction(instruction, word)
                with self.assertRaisesRegex(ValueError, "does not match fresh producer encoder"):
                    self.validate(text=text, rows=rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path,
                        help="本次 fresh producer outputs，含 bconv/")
    parser.add_argument("--encodings", required=True, type=Path,
                        help="同次构建的原始 producer encoder_words.tsv")
    args, remaining = parser.parse_known_args()
    BconvDeliveryTests.source = args.source.resolve(strict=True)
    BconvDeliveryTests.encodings = args.encodings.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
