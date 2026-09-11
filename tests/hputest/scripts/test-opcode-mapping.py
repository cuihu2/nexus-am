#!/usr/bin/env python3
"""验证原生 opcode 接收门禁，不执行 RISC-V/HPU 指令。"""

import unittest

from hpu_opcode_mapping import map_c, map_word


class OpcodeMappingTests(unittest.TestCase):
    def test_control_and_compute_vectors(self):
        vectors = (
            ("PSYNC", 0x7000005B, 0x7000005B),
            ("PMODLD", 0x6001805B, 0x6001805B),
            ("PADD", 0x0441005B, 0x0441005B),
            ("PFREE", 0x8000005B, 0x8000005B),
            ("PNTT", 0x4000FC5B, 0x4000FC5B),
            ("PINTT", 0x5000FC5B, 0x5000FC5B),
        )
        for name, original, expected in vectors:
            with self.subTest(instruction=name):
                self.assertEqual(map_word(original), expected)
                self.assertEqual(map_word(original) >> 7, original >> 7)

    def test_flag_bit_is_not_overwritten(self):
        # flag 位于 bit 7；原生低字节 0xDB 必须保留，不能误写成 0x5B。
        for word in (0x4000FCDB, 0x5000FCDB, 0x044100DB, 0xFFFFFFDB):
            with self.subTest(word=f"{word:08X}"):
                mapped = map_word(word)
                self.assertEqual(mapped & 0xFF, 0xDB)
                self.assertEqual(mapped >> 7, word >> 7)

    def test_each_payload_bit_is_preserved(self):
        # cmd_kind=0 时 cmd26 就是 inst[31:7]；逐位检查包含最高位和 bit 7。
        for payload in (0, (1 << 25) - 1, *(1 << bit for bit in range(25))):
            with self.subTest(payload=payload):
                word = (payload << 7) | 0x5B
                mapped = map_word(word)
                self.assertEqual(mapped & 0x7F, 0x5B)
                self.assertEqual(mapped >> 7, payload)

    def test_dma_words_are_unchanged(self):
        for word in (0x0000002B, 0x000000AB, 0x00B5012B,
                     0x00B541AB, 0xFFFFFF2B, 0xFFFFFFAB):
            with self.subTest(word=f"{word:08X}"):
                self.assertEqual(map_word(word), word)

    def test_unknown_and_legacy_opcodes_are_rejected(self):
        for opcode in range(128):
            if opcode in (0x5B, 0x2B):
                continue
            with self.subTest(opcode=opcode):
                with self.assertRaisesRegex(RuntimeError, "unexpected HPU source opcode"):
                    map_word(0x70000000 | opcode)

    def test_invalid_uint32_inputs_are_rejected(self):
        for word in (-1, 0x100000000, None, "0x7000000B", 1.0, True):
            with self.subTest(word=word):
                with self.assertRaisesRegex(RuntimeError, "uint32"):
                    map_word(word)

    def test_c_preserves_non_instruction_text_and_dma(self):
        source = '''const unsigned data = 0x7000000B;
register unsigned long offset __asm__("x10") = 0;
register unsigned long count __asm__("x11") = 64;
__asm__ volatile(".word 0x7000005B; .word 0x4000FCDB" : : : "memory");
__asm__ volatile(".word 0x00b5012b" : : "r"(offset), "r"(count) : "memory");
'''
        expected = source
        self.assertEqual(map_c(source), expected)

    def test_c_retains_spacing(self):
        self.assertEqual(map_c('".word\t 0x6000005B\\n"'),
                         '".word\t 0x6000005B\\n"')

    def test_c_without_complete_word_markers_is_unchanged(self):
        for source in ("", "0x7000000B", ".word 0x7000000B0",
                       ".word 0x7000000Bu", ".word0x7000000B",
                       "other.word 0x7000000B"):
            with self.subTest(source=source):
                self.assertEqual(map_c(source), source)

    def test_c_rejects_non_hpu_and_legacy_but_native_is_idempotent(self):
        with self.assertRaises(RuntimeError):
            map_c('__asm__(".word 0x00000013");')
        with self.assertRaises(RuntimeError):
            map_c('__asm__(".word 0x7000000B");')
        source = '__asm__(".word 0x7000005B");'
        self.assertEqual(map_c(map_c(source)), source)


if __name__ == "__main__":
    unittest.main()
