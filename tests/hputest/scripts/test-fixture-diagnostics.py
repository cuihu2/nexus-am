#!/usr/bin/env python3
"""编译实际 fixture.h，检查首错诊断、返回值和 cache 操作。

仅在主机上替换 cache/layout/klib；四个系数的常量数组用于注入错误，
不修改实际用例数据，也不模拟 HPU、RISC-V 或实际 DMA/cache 一致性。
"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


TEST_ROOT = Path(__file__).resolve().parents[1]
MODULUS = 17
INPUT_A = (1, 2, 3, 4)
INPUT_B = (5, 6, 7, 8)
GOLDEN = tuple(a * b % MODULUS for a, b in zip(INPUT_A, INPUT_B))
MU = ((1 << 64) - 1) // MODULUS
MOD_CONTEXT = (MODULUS, MU & 0xffffffff, MU >> 32, 0, 0, 0, 0, 0)

MOCK_LAYOUT = r"""
#ifndef MOCK_LAYOUT_H
#define MOCK_LAYOUT_H
#include <stdint.h>
#define HPU_RNS_COEFFICIENTS 4U
#define HPU_WORDS_PER_LINE 8U
#define HPU_MODULUS 17U
#define HPU_RNS_BYTES (HPU_RNS_COEFFICIENTS * sizeof(uint32_t))
#define HPU_LINE_BYTES (HPU_WORDS_PER_LINE * sizeof(uint32_t))
#define HPU_LINE_OUTPUT 1U
#define HPU_LINE_MOD 2U
volatile uint32_t *hpu_line(unsigned line);
void hpu_fence(void);
#endif
"""

MOCK_CACHE = r"""
#ifndef MOCK_CACHE_H
#define MOCK_CACHE_H
#include <stdint.h>
#include <stddef.h>
void hpu_cache_clean(uintptr_t address, size_t bytes);
void hpu_cache_invalidate(uintptr_t address, size_t bytes);
#endif
"""

HARNESS = r"""
#include <assert.h>
#include <stddef.h>
#include <hpu/fixture.h>

static unsigned invalidates, line_requests;

volatile uint32_t *hpu_line(unsigned line) {
    assert(line == HPU_LINE_OUTPUT);
    ++line_requests;
    return output_address;
}

void hpu_cache_invalidate(uintptr_t address, size_t bytes) {
    assert(address == (uintptr_t)output_address);
    assert(bytes == HPU_RNS_BYTES);
    ++invalidates;
}

void hpu_cache_clean(uintptr_t address, size_t bytes) {
    (void)address;
    (void)bytes;
    assert(0 && "validation must not clean cache");
}

void hpu_fence(void) {
    assert(0 && "validation must not add a fence");
}
"""


def c_array(name, values, qualifier="const"):
    words = ", ".join(f"UINT32_C(0x{word:x})" for word in values)
    return f"{qualifier} uint32_t {name}[{len(values)}] = {{{words}}};\n"


class FixtureDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = TEST_ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(
            prefix="fixture-diagnostics-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        mocks = cls.root / "hpu"
        mocks.mkdir()
        (mocks / "layout.h").write_text(MOCK_LAYOUT, encoding="utf-8")
        (mocks / "cache.h").write_text(MOCK_CACHE, encoding="utf-8")
        (cls.root / "klib.h").write_text("#include <stdio.h>\n", encoding="utf-8")

    def run_fixture(self, function, expected_rc=1, *, a=INPUT_A, b=INPUT_B,
                    golden=GOLDEN, mod_context=MOD_CONTEXT, output=GOLDEN,
                    null_output=False):
        # 数组内容在编译期确定，不通过写 const 内存注入错误。
        source = "#include <stdint.h>\n#include <stddef.h>\n"
        for name, values in (("RNS_A", a), ("RNS_B", b),
                             ("RNS_EXPECTED", golden),
                             ("RNS_MOD_CTX", mod_context)):
            source += c_array(name, values)
        if null_output:
            # golden 首项失败时，短路必须阻止对该 NULL 指针的解引用。
            source += "static volatile uint32_t *output_address = NULL;\n"
        else:
            source += c_array("output_data", output, "static volatile")
            source += "static volatile uint32_t *output_address = output_data;\n"
        source += HARNESS
        output_check = int(function in ("check_loopback", "check_pmul"))
        source += f"""
int main(void) {{
    const int rc = {function}();
    assert(rc == {expected_rc});
    assert(invalidates == {output_check}U);
    assert(line_requests == {output_check}U);
    return 0;
}}
"""
        c_path = self.root / "fixture-test.c"
        executable = self.root / "fixture-test"
        c_path.write_text(source, encoding="utf-8")
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        compiled = subprocess.run(
            compiler + ["-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
                        "-I", str(self.root), "-I", str(TEST_ROOT / "include"),
                        str(c_path), "-o", str(executable)],
            text=True, capture_output=True)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        executed = subprocess.run([str(executable)], text=True, capture_output=True)
        self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
        return executed.stdout

    def expect_failure(self, text, *details):
        self.assertEqual(text.count("[HPU][FAIL]"), 1, text)
        self.assertEqual(len(text.splitlines()), 1, text)
        for detail in details:
            self.assertIn(detail, text)

    def test_success_is_silent(self):
        for function in ("fixture_validate", "fixture_validate_mm",
                         "check_loopback", "check_pmul"):
            with self.subTest(function=function):
                output = INPUT_A if function == "check_loopback" else GOLDEN
                self.assertEqual(self.run_fixture(function, 0, output=output), "")

    def test_input_bounds_identify_a_or_b(self):
        for name, original in (("A", INPUT_A), ("B", INPUT_B)):
            with self.subTest(input=name):
                invalid = list(original)
                invalid[1] = MODULUS
                text = self.run_fixture("fixture_validate", **{name.lower(): invalid})
                self.expect_failure(text, "[fixture]", f"input={name}",
                                    "index=1", "actual=0x11", "expected=<q", "q=17")

    def test_mod_context_identifies_each_record_word(self):
        for index in range(4):
            with self.subTest(word=index):
                invalid = list(MOD_CONTEXT)
                invalid[index] ^= 1
                text = self.run_fixture("fixture_validate_mm", mod_context=invalid)
                self.expect_failure(text, "[mod-context]", f"word={index}",
                                    f"actual=0x{invalid[index]:x}",
                                    f"expected=0x{MOD_CONTEXT[index]:x}", "q=17")

    def test_mod_padding_identifies_index(self):
        invalid = list(MOD_CONTEXT)
        invalid[6] = 0x12
        text = self.run_fixture("fixture_validate_mm", mod_context=invalid)
        self.expect_failure(text, "[mod-context-padding]", "index=6",
                            "actual=0x12", "expected=0x0", "q=17")

    def test_fixture_golden_error_is_not_an_hpu_output_error(self):
        invalid = list(GOLDEN)
        invalid[2] = 0x11
        text = self.run_fixture("fixture_validate_mm", golden=invalid, null_output=True)
        self.expect_failure(text, "[fixture-golden-vs-C]", "index=2", "A=0x3",
                            "B=0x7", "golden=0x11", "expected=0x4", "q=17")
        self.assertNotIn("HPU-vs-C", text)

    def test_pmul_bad_golden_does_not_read_null_output(self):
        invalid = list(GOLDEN)
        invalid[0] = 0x11
        text = self.run_fixture("check_pmul", golden=invalid, null_output=True)
        self.expect_failure(text, "[pmul-golden-vs-C]", "index=0", "A=0x1",
                            "B=0x5", "golden=0x11", "expected=0x5", "q=17")
        self.assertNotIn("actual=", text)
        self.assertNotIn("HPU-vs-C", text)

    def test_pmul_output_mismatch_reports_first_actual_and_expected(self):
        invalid = list(GOLDEN)
        invalid[2:] = [0xa5a50002, 0xa5a50003]
        text = self.run_fixture("check_pmul", output=invalid)
        self.expect_failure(text, "[pmul-HPU-vs-C]", "index=2", "A=0x3",
                            "B=0x7", "actual=0xa5a50002", "expected=0x4", "q=17")
        self.assertNotIn("golden-vs-C", text)
        self.assertNotIn("index=3", text)

    def test_loopback_reports_first_actual_and_expected(self):
        invalid = list(INPUT_A)
        invalid[1:] = [0xa5a50001, 0xa5a50002, 0xa5a50003]
        text = self.run_fixture("check_loopback", output=invalid)
        self.expect_failure(text, "[loopback]", "input=A", "index=1",
                            "actual=0xa5a50001", "expected=0x2", "q=17")
        self.assertNotIn("index=2", text)


if __name__ == "__main__":
    unittest.main()
