#!/usr/bin/env python3
"""在主机编译实际 it_v2_memory.c，验证数据/guard/oracle 的软件行为。

DDR 地址使用 host 数组，cache/fence 只计数，不访问真实 MMIO、DDR 或 HPU。
所有编译产物位于 hputest/build 的临时目录，结束后自动清理。
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


TEST_ROOT = Path(__file__).resolve().parents[1]

MOCK_STEPS = r"""
#ifndef MOCK_STEPS_H
#define MOCK_STEPS_H
#include <stddef.h>
#include <stdint.h>
#define WINDOW_LINES 512U
#define WORDS_PER_LINE 64U
#define LINE_BYTES 256U
#define POLY_WORDS 4096U
#define POLY_LINES 64U
#define LINE_A 0U
#define LINE_B 64U
#define LINE_OUT 128U
#define LINE_MOD 192U
extern uint32_t host_memory[WINDOW_LINES * WORDS_PER_LINE];
#define MEM_BASE ((uintptr_t)host_memory)
volatile uint32_t *ddr_line(unsigned line);
void clean_lines(unsigned line, unsigned count);
void invalidate_lines(unsigned line, unsigned count);
void mem_fence(void);
void hpu_fence(void);
#endif
"""

HARNESS = r"""
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <hpu/it_v2.h>
#include <hpu/report.h>

enum { Q0 = 50061313U, Q1 = 50077697U };
uint32_t host_memory[WINDOW_LINES * WORDS_PER_LINE];
static unsigned cleans, invalidates, fences, address_requests;
static unsigned char invalidated[WINDOW_LINES];
const uint32_t RNS_A[POLY_WORDS] = {0U, 1U, Q0 - 1U, 7U};
const uint32_t RNS_B[POLY_WORDS] = {1U, 0U, 3U, Q0 - 1U};

volatile uint32_t *ddr_line(unsigned line) {
    assert(line < WINDOW_LINES);
    ++address_requests;
    return host_memory + line * WORDS_PER_LINE;
}
void clean_lines(unsigned line, unsigned count) {
    assert(line < WINDOW_LINES && count != 0U && count <= WINDOW_LINES - line);
    ++cleans;
}
void invalidate_lines(unsigned line, unsigned count) {
    assert(line < WINDOW_LINES && count != 0U && count <= WINDOW_LINES - line);
    ++invalidates;
    for (unsigned i = line; i < line + count; ++i) invalidated[i]++;
}
void mem_fence(void) { ++fences; }
void hpu_fence(void) { ++fences; }

static void expect_address(unsigned line, unsigned word) {
    printf("[HARNESS] expected_address=0x%lx\n",
           (unsigned long)(MEM_BASE + line * LINE_BYTES + word * sizeof(uint32_t)));
}

static void check_modulus(uint32_t q0, uint32_t q1) {
    const uint32_t *table = v2_expected(LINE_MOD);
    const uint32_t qs[] = {q0, q1};
    const unsigned bases[] = {0U, 24U};

    for (unsigned k = 0U; k < 2U; ++k) {
        /* 用更宽的主机整数独立计算，不能复制待测函数的 UINT64_MAX/q 公式。 */
        const uint64_t mu = (uint64_t)(((unsigned __int128)1U << 64U) / qs[k]);
        const uint32_t *record = table + bases[k];
        assert(record[0] == qs[k]);
        assert(record[1] == (uint32_t)mu);
        assert(record[2] == (uint32_t)(mu >> 32U));
        assert(record[2] < 65536U);
        assert(record[3] == 0U);
    }
    for (unsigned k = 0U; k < WORDS_PER_LINE; ++k) {
        if (k >= 4U && (k < 24U || k >= 28U)) assert(table[k] == 0U);
    }
}

static void profiles(void) {
    assert(v2_prepare(0U, Q0, Q1) == 0);
    assert(cleans == 1U && invalidates == 0U);
    check_modulus(Q0, Q1);
    for (unsigned i = 0U; i < POLY_WORDS; ++i) {
        assert(v2_expected(LINE_A)[i] == RNS_A[i]);
        assert(v2_expected(LINE_B)[i] == RNS_B[i]);
    }
    assert(v2_check_memory("basic") == 0);
    assert(v2_prepare(0U, 65537U, Q1) == 0);
    check_modulus(65537U, Q1);
    for (unsigned i = 0U; i < POLY_WORDS; ++i)
        assert(v2_expected(LINE_A)[i] == RNS_A[i] % 65537U);

    assert(v2_prepare(1U, Q0, Q1) == 0);
    const uint32_t a[] = {0U, 1U, Q0 - 1U, Q0 - 2U, 0U, 1U, Q0 - 1U, Q0 - 2U};
    const uint32_t b[] = {0U, 1U, 1U, Q0 - 1U, Q0 - 1U, Q0 - 2U, Q0 - 1U, Q0 - 2U};
    for (unsigned i = 0U; i < POLY_WORDS; ++i) {
        assert(v2_expected(LINE_A)[i] == a[i % 8U]);
        assert(v2_expected(LINE_B)[i] == b[i % 8U]);
    }
    check_modulus(Q0, Q1);
    assert(v2_check_memory("boundary") == 0);
}

static void invalid_prepare(void) {
    assert(v2_expected(0U) == NULL);
    assert(v2_fill(0U, 1U, 1U) == 1);
    assert(v2_copy(0U, RNS_A, 1U) == 1);
    assert(v2_check_memory("unprepared") == 1);
    assert(v2_prepare(2U, Q0, Q1) == 1);
    assert(v2_prepare(0U, 0U, Q1) == 1);
    assert(v2_prepare(0U, Q0, 65536U) == 1);
    assert(address_requests == 0U && cleans == 0U && invalidates == 0U);
}

static void invalid_ranges(void) {
    assert(v2_prepare(0U, Q0, Q1) == 0);
    const unsigned accesses = address_requests;
    const unsigned clean_count = cleans;
    assert(v2_expected(WINDOW_LINES) == NULL);
    assert(v2_fill(WINDOW_LINES, 1U, 1U) == 1);
    assert(v2_fill(WINDOW_LINES - 1U, 1U, 65U) == 1);
    assert(v2_fill(0U, 1U, UINT_MAX) == 1);
    assert(v2_copy(0U, NULL, 1U) == 1);
    assert(v2_copy(0U, v2_expected(WINDOW_LINES - 1U), 65U) == 1);
    assert(v2_allow_output(WINDOW_LINES - 1U, 2U) == 1);
    assert(v2_allow_output(0U, UINT_MAX) == 1);
    assert(v2_allow_output(0U, 0U) == 1);
    assert(v2_check_words("count", 0U, RNS_A, 0U, Q0) == 1);
    assert(v2_check_words("range", WINDOW_LINES - 1U, RNS_A, 65U, Q0) == 1);
    assert(v2_check_words(NULL, 0U, RNS_A, 1U, Q0) == 1);
    assert(v2_check_words("golden", 0U, NULL, 1U, Q0) == 1);
    assert(v2_check_words("shadow-end", 0U,
                          v2_expected(WINDOW_LINES - 1U), 65U, Q0) == 1);
    assert(v2_check_memory(NULL) == 1);
    assert(address_requests == accesses && cleans == clean_count && invalidates == 0U);
}

static void shadow_and_guard(int guard) {
    assert(v2_prepare(0U, Q0, Q1) == 0);
    const unsigned line = guard ? 500U : LINE_A;
    const unsigned first = guard ? 7U : 0U;
    const uint32_t expected = v2_expected(line)[first];
    host_memory[line * WORDS_PER_LINE + first] ^= 1U;
    host_memory[line * WORDS_PER_LINE + first + 1U] ^= 1U;
    assert(v2_expected(line)[first] == expected);
    expect_address(line, first);
    assert(v2_check_memory(guard ? "guard-corruption" : "input-corruption") == 1);
    assert(invalidates == 1U);
}

static void permissions(void) {
    assert(v2_prepare(0U, Q0, Q1) == 0);
    host_memory[LINE_OUT * WORDS_PER_LINE] ^= 1U;
    assert(v2_check_memory("not-allowed") == 1);
    assert(v2_allow_output(LINE_OUT, POLY_LINES) == 0);
    assert(v2_check_memory("allowed") == 0);
    host_memory[LINE_B * WORDS_PER_LINE] ^= 1U;
    assert(v2_check_memory("readonly-still-protected") == 1);
    assert(v2_prepare(1U, Q0, Q1) == 0);
    host_memory[LINE_OUT * WORDS_PER_LINE] ^= 1U;
    assert(v2_check_memory("permission-reset") == 1);
}

static void invalidate_partition(void) {
    assert(v2_prepare(0U, Q0, Q1) == 0);
    assert(v2_allow_output(LINE_OUT, POLY_LINES) == 0);
    assert(v2_check_memory("partition") == 0);
    for (unsigned i = 0U; i < WINDOW_LINES; ++i)
        assert(invalidated[i] == (i >= LINE_OUT && i < LINE_OUT + POLY_LINES ? 0U : 1U));
    /* 结果比较独立失效输出，最终所有line恰好一次，没有遗漏或重复CBO。 */
    assert(v2_check_words("partition-output", LINE_OUT, v2_expected(LINE_OUT),
                          POLY_WORDS, 0U) == 0);
    for (unsigned i = 0U; i < WINDOW_LINES; ++i) assert(invalidated[i] == 1U);
}

static void copy_and_fill(void) {
    uint32_t data[70];
    assert(v2_prepare(0U, Q0, Q1) == 0);
    for (unsigned i = 0U; i < 70U; ++i) data[i] = 100U + i;
    assert(v2_copy(300U, data, 70U) == 0);
    assert(v2_copy(301U, v2_expected(300U), 70U) == 0);
    for (unsigned i = 0U; i < 70U; ++i) {
        assert(v2_expected(301U)[i] == data[i]);
        assert(host_memory[301U * WORDS_PER_LINE + i] == data[i]);
    }
    const uint32_t tail = v2_expected(400U)[65U];
    assert(v2_fill(400U, 0xdeadU, 65U) == 0);
    for (unsigned i = 0U; i < 65U; ++i) assert(v2_expected(400U)[i] == 0xdeadU);
    assert(v2_expected(400U)[65U] == tail);
    assert(v2_check_memory("after-copy-fill") == 0);
    assert(cleans == 4U);
}

static void compare_words(void) {
    const uint32_t golden[] = {0U, 1U, 17U, Q0 - 1U};
    assert(v2_prepare(0U, Q0, Q1) == 0);
    memcpy(host_memory + LINE_OUT * WORDS_PER_LINE, golden, sizeof(golden));
    assert(v2_check_words("match", LINE_OUT, golden, 4U, Q0) == 0);
    host_memory[LINE_OUT * WORDS_PER_LINE + 2U] = 23U;
    host_memory[LINE_OUT * WORDS_PER_LINE + 3U] = 24U;
    expect_address(LINE_OUT, 2U);
    assert(v2_check_words("first-mismatch", LINE_OUT, golden, 4U, Q0) == 1);
    assert(invalidates == 2U);
}

static void noncanonical(void) {
    const uint32_t raw = Q0;
    assert(v2_prepare(0U, Q0, Q1) == 0);
    host_memory[LINE_OUT * WORDS_PER_LINE] = raw;
    expect_address(LINE_OUT, 0U);
    assert(v2_check_words("noncanonical", LINE_OUT, &raw, 1U, Q0) == 1);
    assert(v2_check_words("raw-word", LINE_OUT, &raw, 1U, 0U) == 0);
}

static void power_of_two_modulus(void) {
    /* API 允许 q>=65537，未要求素数；q=2^17 不是未定义输入。 */
    assert(v2_prepare(0U, 131072U, Q1) == 0);
    check_modulus(131072U, Q1);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(result_context("memory-unit", 0U) == 0);
    if (!strcmp(argv[1], "profiles")) profiles();
    else if (!strcmp(argv[1], "invalid_prepare")) invalid_prepare();
    else if (!strcmp(argv[1], "invalid_ranges")) invalid_ranges();
    else if (!strcmp(argv[1], "shadow")) shadow_and_guard(0);
    else if (!strcmp(argv[1], "guard")) shadow_and_guard(1);
    else if (!strcmp(argv[1], "permissions")) permissions();
    else if (!strcmp(argv[1], "invalidate_partition")) invalidate_partition();
    else if (!strcmp(argv[1], "copy_fill")) copy_and_fill();
    else if (!strcmp(argv[1], "compare")) compare_words();
    else if (!strcmp(argv[1], "noncanonical")) noncanonical();
    else if (!strcmp(argv[1], "power_of_two")) power_of_two_modulus();
    else assert(0 && "unknown test scenario");
    printf("[HARNESS] PASS %s fences=%u\n", argv[1], fences);
    return 0;
}
"""


class V2MemoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = TEST_ROOT / "build"
        build.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="v2-memory-", dir=build)
        cls.addClassCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / "hpu").mkdir()
        (root / "hpu" / "steps.h").write_text(MOCK_STEPS, encoding="utf-8")
        (root / "klib.h").write_text(
            "#include <stdio.h>\n#include <string.h>\n", encoding="utf-8")
        harness = root / "harness.c"
        harness.write_text(HARNESS, encoding="utf-8")
        cls.executable = root / "test-v2-memory"
        command = shlex.split(os.environ.get("HOST_CC", "cc")) + [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2",
            f"-I{root}", f"-I{TEST_ROOT / 'include'}",
            str(harness), str(TEST_ROOT / "runtime" / "it_v2_memory.c"),
            str(TEST_ROOT / "runtime" / "it_report.c"),
            "-o", str(cls.executable),
        ]
        compiled = subprocess.run(command, text=True, capture_output=True, check=False)
        if compiled.returncode:
            raise AssertionError(f"host compile failed:\n{compiled.stdout}{compiled.stderr}")

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario],
                                text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"[HARNESS] PASS {scenario}", result.stdout)
        return result.stdout

    def assert_first_error(self, output, phase, line, index):
        errors = [item for item in output.splitlines() if "[HPU][FAIL]" in item]
        self.assertEqual(len(errors), 1, output)
        address = re.search(r"expected_address=(0x[0-9a-f]+)", output)
        self.assertIsNotNone(address, output)
        self.assertIn(f"[{phase}]", errors[0])
        self.assertIn(f"addr={address.group(1)}", errors[0])
        self.assertIn(f"line={line} index={index}", errors[0])
        self.assertIn("actual=0x", errors[0])
        self.assertIn("expected=0x", errors[0])

    def test_profiles_and_modulus_contexts(self):
        self.assertNotIn("[FAIL]", self.run_scenario("profiles"))

    def test_unprepared_and_invalid_configuration_do_not_access_ddr(self):
        self.run_scenario("invalid_prepare")

    def test_invalid_ranges_pointers_and_counts_do_not_access_ddr(self):
        self.run_scenario("invalid_ranges")

    def test_shadow_survives_input_corruption_and_reports_first_word(self):
        self.assert_first_error(self.run_scenario("shadow"), "input-corruption", 0, 0)

    def test_guard_corruption_reports_first_word(self):
        self.assert_first_error(self.run_scenario("guard"), "guard-corruption", 500, 7)

    def test_output_permissions_are_explicit_and_reset_each_prepare(self):
        output = self.run_scenario("permissions")
        self.assertEqual(output.count("[HPU][FAIL]"), 3, output)
        for phase in ("not-allowed", "readonly-still-protected", "permission-reset"):
            self.assertIn(f"[{phase}][readonly-or-guard]", output)

    def test_copy_overlap_fill_and_shadow_remain_consistent(self):
        self.assertNotIn("[FAIL]", self.run_scenario("copy_fill"))

    def test_readonly_and_output_invalidate_exactly_once(self):
        self.assertNotIn("[FAIL]", self.run_scenario("invalidate_partition"))

    def test_golden_comparison_reports_all_mismatch_statistics(self):
        output = self.run_scenario("compare")
        self.assertIn("phase=first-mismatch round=0 words=4 mismatches=2 first_bad=2", output)
        self.assertIn("DATA,2,0x17,0x11,50061313,6", output)

    def test_noncanonical_equal_words_do_not_pass_modular_comparison(self):
        output = self.run_scenario("noncanonical")
        self.assertIn("phase=noncanonical round=0 words=1 mismatches=1 first_bad=0", output)
        self.assertIn("noncanonical=1 exact_integer=1", output)

    def test_power_of_two_modulus_uses_floor_two_to_64(self):
        self.assertNotIn("[FAIL]", self.run_scenario("power_of_two"))


if __name__ == "__main__":
    unittest.main()
