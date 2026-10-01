#!/usr/bin/env python3
"""Test application-package validation and target-side result/guard checks."""

import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "application_import", Path(__file__).with_name("import-application-package.py"))
IMPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORT)

HARNESS = r'''
#include <hpu/application_case.h>
static uint32_t memory[HPU_APPLICATION_LINES * WORDS_PER_LINE];
volatile uint32_t *ddr_line(unsigned line) { return memory + line * WORDS_PER_LINE; }
void clean_lines(unsigned first, unsigned lines) { (void)first; (void)lines; }
void invalidate_lines(unsigned first, unsigned lines) { (void)first; (void)lines; }
int result_compare(const char *phase, volatile const uint32_t *actual,
                   const uint32_t *golden, unsigned words, uint32_t q) {
    (void)phase;
    for (unsigned i = 0; i < words; ++i)
        if (actual[i] != golden[i] || actual[i] >= q) return 1;
    return 0;
}
int main(void) {
    if (application_prepare() || application_check_memory() || !application_check_results()) return 1;
    for (unsigned i = 0; i < HPU_APPLICATION_GOLDEN_COUNT; ++i) {
        const struct hpu_application_output *out = &hpu_application_outputs[i];
        for (unsigned word = 0; word < out->padded_words; ++word)
            ddr_line(out->line)[word] = application_golden[out->golden_word + word];
    }
    if (application_check_results() || application_check_memory()) return 2;
    const struct hpu_application_output *last =
        &hpu_application_outputs[HPU_APPLICATION_GOLDEN_COUNT - 1];
    ddr_line(last->line)[last->padded_words - 1] ^= 1U;
    if (!application_check_results() || application_check_memory()) return 3;
    ddr_line(last->line)[last->padded_words - 1] ^= 1U;
    memory[0] ^= 1U;
    if (!application_check_memory()) return 4;
    memory[0] ^= 1U;
    memory[HPU_APPLICATION_LINES * WORDS_PER_LINE - 1] ^= 1U;
    if (!application_check_memory()) return 5;
    memory[HPU_APPLICATION_LINES * WORDS_PER_LINE - 1] ^= 1U;
    for (unsigned line = 0; line < HPU_APPLICATION_LINES; ++line) {
        if (application_writable[line]) { ddr_line(line)[0] ^= 1U; break; }
    }
    if (application_check_memory()) return 6;
    return 0;
}
'''


class ApplicationDeliveryTests(unittest.TestCase):
    def test_real_package_and_oracles(self):
        commit, config, image, golden, mask, outputs = IMPORT.validate(
            ARGS.source, ARGS.validator, ARGS.producer_commit)
        case = IMPORT.load_json(ARGS.source / "package.json")["case_name"]
        spec = IMPORT.CASE_SPECS[case]
        self.assertEqual(commit, ARGS.producer_commit)
        self.assertEqual(config, {"capacity_lines": spec["capacity_lines"],
                                  "used_lines": spec["used_lines"]})
        self.assertEqual(len(image), (spec["used_lines"] + IMPORT.GUARD_LINES) * 256)
        golden_count = sum(2 * len(moduli) for moduli in spec["golden_moduli"])
        self.assertEqual(len(golden), golden_count * 128 * 4)
        self.assertEqual(len(mask), spec["used_lines"] + IMPORT.GUARD_LINES)
        self.assertEqual(len(outputs), golden_count)

    def test_upstream_validator_rejects_corrupt_golden(self):
        with tempfile.TemporaryDirectory(prefix="application-corrupt-") as directory:
            package = Path(directory) / "package"
            shutil.copytree(ARGS.source, package)
            golden = next((package / "golden/objects").rglob("*.u32.bin"))
            data = bytearray(golden.read_bytes())
            data[0] ^= 1
            golden.write_bytes(data)
            result = subprocess.run([str(ARGS.validator), str(package)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_runtime_rejects_missing_results_mismatch_and_memory_corruption(self):
        with tempfile.TemporaryDirectory(prefix="application-runtime-") as directory:
            root = Path(directory)
            (root / "hpu").mkdir()
            (root / "klib.h").write_text("#include <stdio.h>\n", encoding="utf-8")
            (root / "hpu/steps.h").write_text(
                "#include <stdint.h>\n#define WORDS_PER_LINE 64U\n"
                "volatile uint32_t *ddr_line(unsigned);\n"
                "void clean_lines(unsigned, unsigned);\n"
                "void invalidate_lines(unsigned, unsigned);\n", encoding="utf-8")
            (root / "harness.c").write_text(HARNESS, encoding="utf-8")
            executable = root / "application-runtime"
            subprocess.run([
                "cc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DHPU_LOG_LEVEL=0", f"-I{root}", f"-I{ROOT / 'include'}",
                f"-I{ARGS.generated}", f"-Wa,-I{ARGS.generated}",
                str(ROOT / "src/common/it_application.c"),
                str(ROOT / "src/common/it_application_fixture.S"),
                str(root / "harness.c"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--producer-commit", required=True)
    ARGS, extra = parser.parse_known_args()
    unittest.main(argv=[__file__, *extra])
