#!/usr/bin/env python3
"""Host checks for CMB_005 Auto fixture/runtime wiring; no HPU command is issued."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("cc") or shutil.which("gcc")

HARNESS = r"""
#include <hpu/auto_case.h>

const uint32_t auto_window[AUTO_TOTAL_WORDS] = {0};
const uint32_t auto_golden[AUTO_COMPONENTS * AUTO_Q_COUNT * AUTO_N] = {0};
static uint32_t memory[AUTO_TOTAL_WORDS];

volatile uint32_t *ddr_line(unsigned line) { return memory + line * WORDS_PER_LINE; }
void clean_lines(unsigned first, unsigned lines) { (void)first; (void)lines; }
void invalidate_lines(unsigned first, unsigned lines) { (void)first; (void)lines; }
int result_compare(const char *phase, volatile const uint32_t *actual,
                   const uint32_t *golden, unsigned words, uint32_t q) {
    (void)phase;
    for (unsigned index = 0; index < words; ++index)
        if (actual[index] != golden[index] || (q != 0 && actual[index] >= q)) return 1;
    return 0;
}

int main(void) {
    if (auto_prepare() != 0 || auto_check_results() != 0 ||
        auto_check_memory() != 0) return 1;
    memory[HPU_AUTO_OUTPUT_OFFSET * WORDS_PER_LINE + 7] = 1;
    if (auto_check_results() == 0 || auto_check_memory() != 0) return 2;
    memory[HPU_AUTO_OUTPUT_OFFSET * WORDS_PER_LINE + 7] = 0;
    memory[19] = 1;
    if (auto_check_memory() == 0) return 3;
    memory[19] = 0;
    memory[HPU_AUTO_GUARD_OFFSET * WORDS_PER_LINE + 23] = 1;
    if (auto_check_memory() == 0) return 4;
    memory[HPU_AUTO_GUARD_OFFSET * WORDS_PER_LINE + 23] = 0;
    memory[HPU_AUTO_WORKSPACE_OFFSET * WORDS_PER_LINE + 11] = 1;
    if (auto_check_memory() != 0) return 5;
    return 0;
}
"""


class AutoRuntimeTests(unittest.TestCase):
    delivery = None

    def make_headers(self, root):
        include = root / "include"
        (include / "hpu").mkdir(parents=True)
        (include / "klib.h").write_text(
            "#ifndef KLIB_H\n#define KLIB_H\nint printf(const char *, ...);\n#endif\n",
            encoding="ascii")
        (include / "hpu/inline_asm_mm_delivery.h").write_text(
            "#define HPU_MM_COEFFICIENTS 4096U\n"
            "#define HPU_MM_LINE_SRC_A 0U\n#define HPU_MM_LINES_SRC_A 64U\n"
            "#define HPU_MM_LINE_SRC_B 64U\n#define HPU_MM_LINES_SRC_B 64U\n"
            "#define HPU_MM_LINE_OUTPUT 128U\n#define HPU_MM_LINES_OUTPUT 64U\n"
            "#define HPU_MM_LINE_MOD 192U\n#define HPU_MM_LINES_MOD 1U\n"
            "#define HPU_MM_MODULUS 50061313U\n", encoding="ascii")
        return include

    @unittest.skipUnless(CC, "host C compiler is unavailable")
    def test_output_and_workspace_are_writable_but_input_and_guard_are_not(self):
        if self.delivery is None or not (self.delivery / "auto_delivery.h").is_file():
            self.fail("pass --delivery pointing at generated auto-data")
        with tempfile.TemporaryDirectory(prefix="auto-runtime-") as directory:
            root = Path(directory)
            include = self.make_headers(root)
            harness = root / "harness.c"
            harness.write_text(HARNESS, encoding="ascii")
            executable = root / ("runtime.exe" if os.name == "nt" else "runtime")
            subprocess.run([
                CC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DHPU_LOG_LEVEL=0", f"-I{include}", f"-I{ROOT / 'include'}",
                f"-I{self.delivery}", str(ROOT / "src/common/it_auto.c"),
                str(harness), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)

    @unittest.skipUnless(CC, "host C compiler is unavailable")
    def test_case_and_generated_program_are_warning_clean_c(self):
        with tempfile.TemporaryDirectory(prefix="auto-syntax-") as directory:
            include = self.make_headers(Path(directory))
            subprocess.run([
                CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                "-DHPU_LOG_LEVEL=0", f"-I{include}", f"-I{ROOT / 'include'}",
                f"-I{self.delivery}",
                str(ROOT / "src/04_composite_instruction_sequences/01_composite_operators/HPU_IT_DIR_CMB_005.c"),
                str(self.delivery / "auto.c"),
            ], check=True)

    def test_main_keeps_fault_irq_completion_and_result_checks_visible(self):
        case = ROOT / "src/04_composite_instruction_sequences/01_composite_operators/HPU_IT_DIR_CMB_005.c"
        source = case.read_text(encoding="utf-8")
        ordered = (
            "auto_prepare()", "csr_write(CSR_FAULT, FAULT_VALID)",
            "csr_write(CSR_IRQ, IRQ_LEVEL)", "csr_write(CSR_COMMIT, COMMIT)",
            "hpu_program_auto(auto_spans, HPU_AUTO_DMA_COUNT)", "wait_irq()",
            "completion_clear() != 0 || check_status() != 0",
            "auto_check_results()", "auto_check_memory()",
        )
        positions = [source.index(fragment) for fragment in ordered]
        self.assertEqual(positions, sorted(positions))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--delivery", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    AutoRuntimeTests.delivery = args.delivery.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
