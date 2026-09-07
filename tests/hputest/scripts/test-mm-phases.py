#!/usr/bin/env python3
"""Check that AM's phase adaptation preserves the actual producer body."""
import importlib.util
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "mm_import", Path(__file__).with_name("import-inline-asm-mm.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source_path = Path(sys.argv.pop(1))
source = source_path.read_text(encoding="utf-8")


def words(text):
    return re.findall(r"\.word (0x[0-9a-fA-F]{8})", text)


class PhaseTests(unittest.TestCase):
    def test_original_words_and_gpr_bindings_survive(self):
        header, result = module.render_mm_phases(source)
        self.assertIn('#include "mm.h"', header)
        load, compute = result.split("int mm_compute(")
        self.assertEqual(words(load), words(source)[:1])
        self.assertEqual(words(compute), words(source)[1:])
        # Each original DMA block is retained verbatim, including bounds and GPRs.
        dma_blocks = re.findall(r'    /\* \d+: d(?:load|store).*?\n#endif',
                                source, re.DOTALL)
        self.assertEqual(len(dma_blocks), 4)
        for block in dma_blocks:
            self.assertEqual(result.count(block), 1)
        self.assertIn('    /* 1: pmodld 0 */', compute)
        self.assertNotIn('".word 0x7000000B"', load)

    def test_unrecognized_structure_is_rejected(self):
        with self.assertRaises(RuntimeError):
            module.render_mm_phases(source.replace('/* 1: pmodld 0 */', '/* changed */'))

    def test_changed_instruction_is_rejected(self):
        with self.assertRaises(RuntimeError):
            module.render_mm_phases(source.replace('0x6000000B', '0x6000400B'))

    def test_stale_dma_precode_is_rejected(self):
        # 使用真实交付文件，只破坏 cmd26 中的 GPR 字段，确保不能混用旧格式。
        build = Path(__file__).resolve().parents[1] / "build"
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="mm-precode-", dir=build) as tmp:
            root = Path(tmp)
            for name in ("mm.c", "mm.h", "mm.asm", "mm.inst32", "mm.cmd26",
                         "dma_relocation_manifest.csv"):
                shutil.copy2(source_path.parent / name, root / name)
            module.validate_program(root)
            commands = (root / "mm.cmd26").read_text().splitlines()
            commands[0] = f"{int(commands[0], 2) & ~(0x3FF << 15):026b}"
            (root / "mm.cmd26").write_text("\n".join(commands) + "\n")
            with self.assertRaisesRegex(RuntimeError, "mm.cmd26"):
                module.validate_program(root)


if __name__ == "__main__":
    unittest.main()
