#!/usr/bin/env python3
"""针对真实 CKKS 交付验证编码/重定位拒绝路径，以及目标端结果与 guard 检查。"""
import argparse
import copy
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ckks_import", Path(__file__).with_name("import-ckks-data.py"))
IMPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORT)

HARNESS = r'''
#include <hpu/ckks_case.h>
static uint32_t memory[CKKS_LINES * WORDS_PER_LINE];
volatile uint32_t *ddr_line(unsigned line) { return memory + line * WORDS_PER_LINE; }
void clean_lines(unsigned first, unsigned lines) { (void)first; (void)lines; }
void invalidate_lines(unsigned first, unsigned lines) { (void)first; (void)lines; }
int result_compare(const char *phase, volatile const uint32_t *actual,
                   const uint32_t *golden, unsigned words, uint32_t q) {
    (void)phase; (void)q;
    for (unsigned i = 0; i < words; ++i) if (actual[i] != golden[i]) return 1;
    return 0;
}
int main(void) {
    if (ckks_prepare() || ckks_check_memory() || !ckks_check_results()) return 1;
    for (unsigned i = 0; i < CKKS_OUTPUTS; ++i)
        for (unsigned j = 0; j < CKKS_N; ++j)
            ddr_line(ckks_outputs[i].line)[j] = ckks_golden[ckks_outputs[i].golden_word + j];
    if (ckks_check_results() || ckks_check_memory()) return 2;
    ddr_line(ckks_outputs[CKKS_OUTPUTS - 1].line)[CKKS_N - 1] ^= 1;
    if (!ckks_check_results() || ckks_check_memory()) return 3;
    memory[0] ^= 1;
    if (!ckks_check_memory()) return 4;
    memory[0] ^= 1;
    memory[CKKS_LINES * WORDS_PER_LINE - 1] ^= 1;
    if (!ckks_check_memory()) return 5;
    memory[CKKS_LINES * WORDS_PER_LINE - 1] ^= 1;
    for (unsigned line = 0; line < CKKS_LINES; ++line) {
        if (ckks_writable[line]) { ddr_line(line)[0] ^= 1; break; }
    }
    if (ckks_check_memory()) return 6;
    return 0;
}
'''


class DeliveryTests(unittest.TestCase):
    def test_real_profiles(self):
        for profile in IMPORT.PROFILES:
            config, image, golden, mask, spans = IMPORT.validate(
                ARGS.source / profile, profile, ARGS.encoder)
            self.assertEqual(len(image), len(mask) * 256)
            self.assertEqual(len(spans), IMPORT.PROFILES[profile][2] * 2)
            self.assertEqual(len(golden), len(spans) * IMPORT.PROFILES[profile][1] * 4)
            self.assertEqual(config["program_stem"], IMPORT.PROFILES[profile][0])

    def test_rejects_mutated_c_word_and_dma(self):
        source = ARGS.source / "composed"
        code_path = source / "ckks_composed_application.c"
        original_text, original_rows = Path.read_text, IMPORT.rows
        code = code_path.read_text().replace(".word 0x08B540AB", ".word 0x08B5400B", 1)
        def read_text(path, *args, **kwargs):
            return code if path == code_path else original_text(path, *args, **kwargs)
        with patch.object(Path, "read_text", read_text), self.assertRaisesRegex(ValueError, "C instruction"):
            IMPORT.validate(source, "composed", ARGS.encoder)
        def read_rows(path):
            data = copy.deepcopy(original_rows(path))
            if path.name == "dma_relocation_manifest.csv":
                data[0]["line_offset"] = "1"
            return data
        with patch.object(IMPORT, "rows", read_rows), self.assertRaisesRegex(ValueError, "resolved spans"):
            IMPORT.validate(source, "composed", ARGS.encoder)

    def test_encoder_rejects_changed_inst32(self):
        source = ARGS.source / "composed"
        stem = "ckks_composed_application"
        with tempfile.TemporaryDirectory(prefix="ckks-encoding-") as directory:
            words = (source / (stem + ".inst32")).read_text()
            changed = Path(directory) / "changed.inst32"
            changed.write_text(("1" if words[0] == "0" else "0") + words[1:])
            result = subprocess.run([str(ARGS.encoder), str(source / (stem + ".asm")),
                                     str(changed), str(source / (stem + ".cmd26"))],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_actual_runtime_rejects_missing_result_mismatch_and_guard_corruption(self):
        with tempfile.TemporaryDirectory(prefix="ckks-runtime-") as directory:
            root = Path(directory)
            (root / "hpu").mkdir()
            (root / "klib.h").write_text("#include <stdio.h>\n")
            (root / "hpu/steps.h").write_text(
                "#include <stdint.h>\n#define WORDS_PER_LINE 64U\n"
                "volatile uint32_t *ddr_line(unsigned);\n"
                "void clean_lines(unsigned, unsigned);\n"
                "void invalidate_lines(unsigned, unsigned);\n")
            (root / "harness.c").write_text(HARNESS)
            for profile in IMPORT.PROFILES:
                data = ARGS.generated / "ckks-data" / profile
                executable = root / profile
                subprocess.run([
                    "cc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-DHPU_LOG_LEVEL=0",
                    f"-I{root}", f"-I{ROOT / 'include'}", f"-I{data}", f"-Wa,-I{data}",
                    str(ROOT / "src/common/it_ckks.c"), str(ROOT / "src/common/it_ckks_fixture.S"),
                    str(root / "harness.c"), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True, help="root containing polynomial/composed")
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--encoder", type=Path, required=True)
    ARGS, extra = parser.parse_known_args()
    unittest.main(argv=[__file__, *extra])
