#!/usr/bin/env python3
"""以架构 hook 运行实际 it_progress.c，核对参数选择与 cycle 阶段统计。"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hpu/progress.h>

static const char *arguments;
static unsigned enabled, sampled;
static uint64_t samples[] = {100U, 200U, 230U, 300U, 370U};

const char *progress_host_mainargs(void) { return arguments; }
void progress_host_enable_cycle(void) { ++enabled; }
uint64_t progress_host_cycle(void) {
    assert(enabled == 1U && sampled < 5U);
    return samples[sampled++];
}

int main(int argc, char **argv) {
    assert(argc == 4);
    arguments = argv[1];
    unsigned count = (unsigned)strtoul(argv[2], NULL, 10);
    const char *mode = argv[3];
    assert(subcase_selected(0U) == 0);
    if (strcmp(mode, "before") == 0) {
        phase_mark("before");
        assert(enabled == 0U && sampled == 0U);
        return 0;
    }
    if (strcmp(mode, "wrap") == 0) {
        samples[0] = UINT64_MAX - 9U;
        samples[1] = 5U;
    }
    int rc = progress_begin(strcmp(mode, "badid") == 0 ? NULL : "CASE", count);
    if (strcmp(mode, "invalid") == 0 || strcmp(mode, "badid") == 0) {
        assert(rc == 1);
        assert(enabled == 0U && sampled == 0U);
        assert(subcase_selected(0U) == 0);
        return 0;
    }
    assert(rc == 0 && enabled == 1U && sampled == 1U);
    if (strcmp(mode, "again") == 0) {
        assert(progress_begin("CASE", count) == 1);
        assert(enabled == 1U && sampled == 1U);
        return 0;
    }
    if (strcmp(mode, "badphase") == 0) {
        phase_mark(NULL);
        assert(enabled == 1U && sampled == 1U);
        return 0;
    }
    if (strcmp(mode, "all") == 0) {
        for (unsigned index = 0U; index < count; ++index)
            assert(subcase_selected(index) == 1);
    } else if (strcmp(mode, "selected") == 0) {
        unsigned chosen = (unsigned)strtoul(arguments + 8, NULL, 10);
        for (unsigned index = 0U; index < count; ++index)
            assert(subcase_selected(index) == (index == chosen));
    }
    assert(subcase_selected(count) == 0 && subcase_selected(UINT_MAX) == 0);
    phase_mark("prepare");
    phase_mark("golden");
    assert(sampled == 5U && enabled == 1U);
    return 0;
}
'''


class ProgressTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="progress-host-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        (folder / "klib.h").write_text("#include <stdio.h>\n#include <string.h>\n")
        (folder / "harness.c").write_text(HARNESS)
        cls.executable = folder / "progress-test"
        subprocess.run(
            shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-DHPU_PROGRESS_HOST_TEST", "-I", str(folder),
                "-I", str(ROOT / "include"), str(ROOT / "runtime/it_progress.c"),
                str(folder / "harness.c"), "-o", str(cls.executable),
            ], check=True,
        )

    def run_case(self, argument="", count=4, mode="all"):
        return subprocess.run(
            [str(self.executable), argument, str(count), mode],
            check=True, capture_output=True, text=True,
        ).stdout

    def test_empty_defaults_to_all(self):
        self.assertIn("selection=all subcases=4 coverage=full", self.run_case())

    def test_explicit_all(self):
        self.assertIn("coverage=full", self.run_case("all", 6))

    def test_selected_first_middle_last(self):
        for index in (0, 2, 3):
            with self.subTest(index=index):
                output = self.run_case(f"subcase={index}", 4, "selected")
                self.assertIn("coverage=selected-subset-not-full", output)
                self.assertIn(f"selection=subcase={index}", output)

    def test_leading_zeroes_are_decimal(self):
        self.assertIn("selection=subcase=2", self.run_case("subcase=002", 4, "selected"))

    def test_invalid_arguments_do_not_touch_architecture(self):
        for argument in ("none", " all", "all ", "subcase", "subcase=", "subcase=-1",
                         "subcase=+1", "subcase=1x", "subcase= 1", "subcase=1 ",
                         "subcase=0x1", "subcase=4", "subcase=4294967296",
                         "subcase=99999999999999999999999"):
            with self.subTest(argument=argument):
                self.assertIn("reason=invalid-mainargs", self.run_case(argument, 4, "invalid"))

    def test_zero_subcases_rejected(self):
        self.assertIn("reason=invalid-mainargs", self.run_case("all", 0, "invalid"))

    def test_invalid_case_id(self):
        self.assertIn("reason=invalid-mainargs", self.run_case("", 4, "badid"))

    def test_phase_logging_excludes_own_print(self):
        output = self.run_case()
        self.assertIn("current=prepare cycle=200 previous=begin cpu_elapsed=100", output)
        self.assertIn("current=golden cycle=300 previous=prepare cpu_elapsed=70", output)
        self.assertIn("not=HPU-pure-compute", output)

    def test_counter_wrap_uses_unsigned_difference(self):
        self.assertIn("cycle=5 previous=begin cpu_elapsed=15", self.run_case("", 4, "wrap"))

    def test_phase_before_begin_does_not_read_cycle(self):
        self.assertIn("reason=invalid-phase-or-not-started", self.run_case("", 4, "before"))

    def test_null_phase_does_not_read_cycle(self):
        self.assertIn("reason=invalid-phase-or-not-started", self.run_case("", 4, "badphase"))

    def test_begin_only_enables_counter_once(self):
        self.assertIn("reason=already-started", self.run_case("", 4, "again"))


if __name__ == "__main__":
    unittest.main()
