#!/usr/bin/env python3
"""校验实际 MM 交付：保留完整程序和唯一末尾 PSYNC，不再拆分阶段。"""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "mm_import", Path(__file__).with_name("import-inline-asm-mm.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source_path = Path(sys.argv.pop(1))
source = source_path.read_text(encoding="utf-8")


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        build = Path(__file__).resolve().parents[1] / "build"
        build.mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="mm-delivery-", dir=build)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for name in ("mm.c", "mm.h", "mm.asm", "mm.inst32", "mm.cmd26",
                     "dma_relocation_manifest.csv"):
            shutil.copy2(source_path.parent / name, self.root / name)

    def test_complete_program_and_bindings(self):
        module.validate_program(self.root)
        words = [int(word, 16) for word in
                 re.findall(r"\.word 0x([0-9a-fA-F]{8})", source)]
        self.assertEqual(words, module.EXPECTED_MM_WORDS)
        self.assertEqual(words.count(0x7000000B), 1)
        self.assertEqual(words[-1], 0x7000000B)
        self.assertEqual(source.count('__asm__("x10")'), 4)
        self.assertEqual(source.count('__asm__("x11")'), 4)
        self.assertNotIn("mm_load_mod", source)
        self.assertNotIn("mm_compute", source)

    def test_extra_psync_is_rejected(self):
        modified = source.replace('".word 0x7000000B"',
                                  '".word 0x7000000B; .word 0x7000000B"')
        self.assertNotEqual(modified, source)
        (self.root / "mm.c").write_text(modified, encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "mm.c"):
            module.validate_program(self.root)

    def test_previous_dma_encoding_is_rejected(self):
        words = (self.root / "mm.inst32").read_text().splitlines()
        words[0] = f"{0x5A820E2B:032b}"
        (self.root / "mm.inst32").write_text("\n".join(words) + "\n")
        with self.assertRaisesRegex(RuntimeError, "mm.inst32"):
            module.validate_program(self.root)

    def test_precode_must_preserve_standard_gpr_fields(self):
        commands = (self.root / "mm.cmd26").read_text().splitlines()
        # 新位段中 rs1/rs2 在 cmd26[12:8]/[17:13]，两者均不得被抹掉。
        commands[0] = f"{int(commands[0], 2) & ~(0x3FF << 8):026b}"
        (self.root / "mm.cmd26").write_text("\n".join(commands) + "\n")
        with self.assertRaisesRegex(RuntimeError, "mm.cmd26"):
            module.validate_program(self.root)

    def test_store_length_guard_must_survive(self):
        modified = source.replace("spans[3].line_count != hpu_obj_len[0]", "0")
        self.assertNotEqual(modified, source)
        (self.root / "mm.c").write_text(modified, encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "OBJ.len"):
            module.validate_program(self.root)

    def test_real_producer_store_length_validation(self):
        # 主机仅执行生成代码的参数检查；-4 表示无 RISC-V 指令执行支持，非 HPU PASS。
        harness = self.root / "test.c"
        harness.write_text('''#include <assert.h>
#include "mm.h"
int main(void) {
    hpu_dma_span_t spans[HPU_PROGRAM_MM_DMA_COUNT] = {
        {192, 1}, {0, 64}, {64, 64}, {128, 64}
    };
    assert(hpu_program_mm(spans, HPU_PROGRAM_MM_DMA_COUNT) == -4);
    spans[3].line_count = 63;
    assert(hpu_program_mm(spans, HPU_PROGRAM_MM_DMA_COUNT) == -5);
    spans[3].line_count = 65;
    assert(hpu_program_mm(spans, HPU_PROGRAM_MM_DMA_COUNT) == -5);
    return 0;
}
''', encoding="utf-8")
        binary = self.root / "test"
        subprocess.run(shlex.split(os.environ.get("HOST_CC", "cc")) + [
            "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(self.root),
            str(self.root / "mm.c"), str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
