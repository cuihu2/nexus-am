#!/usr/bin/env python3
"""编译真实结果模块和 Nexus-AM printf，验证 UART 协议与严格 CSV 导出。

仅 UART 字符输出使用主机 stdout；不模拟 HPU、不读取硬件 DDR/CSR。
临时编译文件放在本仓库 build/，避免占用共享 /tmp。
"""

import csv
import importlib.util
import io
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
AM_ROOT = ROOT.parents[1]
SPEC = importlib.util.spec_from_file_location(
    "uart_parser", ROOT / "scripts/parse-uart-results.py")
PARSER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PARSER)

HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <hpu/report.h>

void _putc(char ch) { fputc(ch, stdout); }
void lock(volatile uint64_t *unused) { (void)unused; }
void release(volatile uint64_t *unused) { (void)unused; }

int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "invalid") == 0) {
        const uint32_t input[] = {1U};
        assert(result_compare("unbound", input, input, 1U, 0U) == 1);
        assert(result_context("has space", 0U) == 1);
        assert(result_context("has,comma", 0U) == 1);
        assert(result_context("", 0U) == 1);
        assert(result_context(NULL, 0U) == 1);
        assert(result_context("valid", 0U) == 0);
        assert(result_compare(NULL, input, input, 1U, 0U) == 1);
        assert(result_compare("", input, input, 1U, 0U) == 1);
        assert(result_compare("bad\nphase", input, input, 1U, 0U) == 1);
        assert(result_compare("phase", NULL, input, 1U, 0U) == 1);
        assert(result_compare("phase", input, NULL, 1U, 0U) == 1);
        assert(result_compare("phase", input, input, 0U, 0U) == 1);
        assert(result_context("bad,context", 0U) == 1);
        assert(result_compare("cleared", input, input, 1U, 0U) == 1);
        return 0;
    }
    assert(result_context("03/HPU_PMAC.c", 3U) == 0);
    puts("[unrelated] do not parse result-looking text: DATA,0,0x1,0x1,0,0");
    if (strcmp(argv[1], "pass") == 0) {
        const uint32_t input[] = {0U, 1U, 2U, 3U, 4U, 96U};
        assert(result_compare("base", input, input, 6U, 97U) == 0);
    } else if (strcmp(argv[1], "mixed") == 0) {
        const uint32_t input[] = {0U, 102U, 7U, 0U, UINT32_MAX, 7U, 97U, 1U};
        const uint32_t golden[] = {0U, 5U, 8U, UINT32_MAX, 0U, 7U, 97U, 1U};
        assert(result_compare("edge", input, golden, 8U, 97U) == 1);
    } else if (strcmp(argv[1], "bounded") == 0) {
        uint32_t input[24], golden[24];
        for (unsigned i = 0U; i < 24U; ++i) {
            input[i] = i < 4U ? 1U : 2U;
            golden[i] = 1U;
        }
        input[23] = 100U;
        assert(result_compare("many-errors", input, golden, 24U, 0U) == 1);
    } else if (strcmp(argv[1], "blocks") == 0) {
        const uint32_t input[] = {0U, UINT32_MAX};
        assert(result_compare("same-phase", input, input, 2U, 0U) == 0);
        assert(result_compare("same-phase", input, input, 2U, 0U) == 0);
        assert(result_context("03/HPU_PMAC.c", 4U) == 0);
        assert(result_compare("same-phase", input, input, 2U, 0U) == 0);
    } else {
        assert(0 && "unknown test scenario");
    }
    return 0;
}
"""


class ReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="result-report-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        (cls.work / "klib.h").write_text('#include "printf.h"\n', encoding="utf-8")
        (cls.work / "am.h").write_text("void _putc(char character);\n", encoding="utf-8")
        (cls.work / "harness.c").write_text(HARNESS, encoding="utf-8")
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        cls.binaries = {}
        for mode in (0, 1):
            binary = cls.work / f"report-{mode}"
            subprocess.run(compiler + [
                "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                f"-DHPU_DUMP_RESULTS={mode}",
                f"-I{cls.work}", f"-I{ROOT / 'include'}",
                f"-I{AM_ROOT / 'libs/klib/include'}",
                str(cls.work / "harness.c"), str(ROOT / "runtime/it_report.c"),
                str(AM_ROOT / "libs/klib/src/printf.c"), "-o", str(binary),
            ], check=True, capture_output=True, text=True)
            cls.binaries[mode] = binary

    @classmethod
    def run_case(cls, mode, scenario):
        return subprocess.run([str(cls.binaries[mode]), scenario], check=True,
                              capture_output=True, text=True).stdout

    def test_full_pass_and_real_am_integer_formatting(self):
        text = self.run_case(1, "pass")
        rows = PARSER.parse_lines(text.splitlines())
        self.assertEqual(len(rows), 6)
        self.assertEqual([row["actual"] for row in rows], [0, 1, 2, 3, 4, 96])
        self.assertTrue(all(row["actual"] == row["expected"] for row in rows))
        self.assertIn("END,6,6,0,-1,0,0,0,PASS", text)

    def test_full_failure_preserves_every_result_and_strict_canonical_check(self):
        text = self.run_case(1, "mixed")
        rows = PARSER.parse_lines(text.splitlines())
        self.assertEqual(len(rows), 8)
        self.assertEqual(rows[3]["signed_delta"], -4294967295)
        self.assertEqual(rows[4]["signed_delta"], 4294967295)
        self.assertEqual(rows[6]["actual"], rows[6]["expected"])
        self.assertEqual(rows[6]["actual"], 97)
        self.assertIn("END,8,8,5,1,4294967295,1,3,FAIL", text)

    def test_brief_output_is_bounded_but_counts_late_errors(self):
        text = self.run_case(0, "bounded")
        data = [line for line in text.splitlines() if line.startswith(PARSER.PREFIX + "DATA,")]
        self.assertEqual(len(data), 12)  # 前 4 项正确，另外最多 8 项错误。
        self.assertIn("END,24,12,20,4,99,0,0,FAIL", text)
        self.assertIn("mismatches=20", text)
        with self.assertRaisesRegex(ValueError, "HPU_DUMP_RESULTS=1"):
            PARSER.parse_lines(text.splitlines())

    def test_repeated_phase_uses_explicit_round_and_unique_block(self):
        rows = PARSER.parse_lines(self.run_case(1, "blocks").splitlines())
        self.assertEqual([(row["round"], row["block"]) for row in rows],
                         [(3, 0), (3, 0), (3, 1), (3, 1), (4, 0), (4, 0)])
        self.assertTrue(all(row["case"] == "03/HPU_PMAC.c" for row in rows))
        self.assertTrue(all(row["phase"] == "same-phase" for row in rows))

    def test_invalid_arguments_do_not_access_input_or_emit_partial_block(self):
        text = self.run_case(1, "invalid")
        self.assertNotIn("BEGIN,", text)
        self.assertNotIn("DATA,", text)
        with self.assertRaisesRegex(ValueError, "error result record"):
            PARSER.parse_lines(text.splitlines())

    def test_parser_rejects_truncation_duplicate_and_forged_summary(self):
        lines = [line for line in self.run_case(1, "pass").splitlines()
                 if line.startswith(PARSER.PREFIX)]
        malformed = {
            "no-END": lines[:-1],
            "no-DATA": lines[:1] + lines[2:],
            "duplicate-index": lines[:2] + [lines[1]] + lines[2:],
            "duplicate-block": lines + lines,
            "nested-BEGIN": [lines[0]] + lines,
            "wrong-q": [line.replace("DATA,0,0x0,0x0,97,0", "DATA,0,0x0,0x0,98,0") for line in lines],
            "wrong-delta": [line.replace("DATA,0,0x0,0x0,97,0", "DATA,0,0x0,0x0,97,-1") for line in lines],
            "wrong-count": [line.replace("END,6,6,0,-1", "END,6,5,0,-1") for line in lines],
            "wrong-pass": [line.replace(",PASS", ",FAIL") for line in lines],
            "unsupported-version": [line.replace("BEGIN,1,", "BEGIN,2,") for line in lines],
            "extra-field": lines[:1] + [lines[1] + ",7"] + lines[2:],
            "data-outside": lines[1:],
            "non-protocol": ["unrelated DATA,0,0x0,0x0,97,0"],
        }
        for label, bad in malformed.items():
            with self.subTest(label=label), self.assertRaises(ValueError):
                PARSER.parse_lines(bad)

    def test_parser_numeric_and_context_validation(self):
        for text in ("", "-0", "+1", " 1", "1_0", "0x", "0X01", "4294967296", "1.0"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                PARSER.number(text)
        for text in ("", "bad context", "bad,context", "bad\ncontext", "中文"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                PARSER.name(text)

    def test_cli_writes_csv_only_after_complete_log_validation(self):
        source = self.work / "source.log"
        output = self.work / "results.csv"
        source.write_text(self.run_case(1, "mixed"), encoding="utf-8")
        command = [os.sys.executable, str(ROOT / "scripts/parse-uart-results.py"),
                   str(source), "--output", str(output)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        original = output.read_text(encoding="utf-8")
        rows = list(csv.DictReader(io.StringIO(original)))
        self.assertEqual(len(rows), 8)
        self.assertEqual(rows[3]["signed_delta"], "-4294967295")
        source.write_text(PARSER.PREFIX + "BEGIN,1,full,x,p,0,0,1,97,0x87000000\n",
                          encoding="utf-8")
        failed = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(output.read_text(encoding="utf-8"), original)
        same_file = subprocess.run(command[:-1] + [str(source)], capture_output=True, text=True)
        self.assertNotEqual(same_file.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
