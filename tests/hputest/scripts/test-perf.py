#!/usr/bin/env python3
"""Host-test PERF timing and CPU NTT/INTT references against generated delivery."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
NTT_WINDOW = ROOT / "build/generated/transform-data/ntt/window.u32.bin"
NTT_GOLDEN = ROOT / "build/generated/transform-data/ntt/golden.u32.bin"
INTT_WINDOW = ROOT / "build/generated/transform-data/intt/window.u32.bin"
INTT_GOLDEN = ROOT / "build/generated/transform-data/intt/golden.u32.bin"
HARNESS = r'''
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <hpu/ntt_reference.h>
#include <hpu/perf.h>

enum { WINDOW_WORDS = 640 * 64, INPUT_WORD = 1 * 64, FACTOR_WORD = 130 * 64 };

static unsigned enabled, sampled;
static uint64_t cycles[] = {100U, 175U};

void perf_host_enable_cycle(void) { ++enabled; }
uint64_t perf_host_cycle(void) {
    assert(enabled == 1U && sampled < 2U);
    return cycles[sampled++];
}

static void read_words(const char *path, uint32_t *words, size_t count) {
    FILE *stream = fopen(path, "rb");
    assert(stream != NULL);
    assert(fread(words, sizeof(*words), count, stream) == count);
    assert(fgetc(stream) == EOF);
    assert(fclose(stream) == 0);
}

int main(int argc, char **argv) {
    struct perf_samples values;
    static struct hpu_ntt_reference ntt_reference, intt_reference;
    static uint32_t ntt_window[WINDOW_WORDS], ntt_golden[HPU_NTT_REFERENCE_N];
    static uint32_t intt_window[WINDOW_WORDS], intt_golden[HPU_NTT_REFERENCE_N];
    assert(argc == 5);
    perf_cycle_enable();
    assert(perf_cycle_read() == 100U);
    assert(perf_cycle_read() == 175U);

    perf_samples_init(&values);
    assert(values.count == 0U && perf_samples_average(&values) == 0U);
    assert(perf_samples_record(&values, 100U) == 0);
    assert(perf_samples_record(&values, 300U) == 0);
    assert(perf_samples_record(&values, 200U) == 0);
    assert(values.count == 3U && values.total == 600U);
    assert(values.minimum == 100U && values.maximum == 300U);
    assert(perf_samples_average(&values) == 200U);
    assert(perf_speedup_x1000(500U, 200U) == 2500U);
    assert(perf_speedup_x1000(1U, 0U) == 0U);
    assert(perf_speedup_x1000(UINT64_MAX - 1U, UINT64_MAX) == 999U);
    assert(perf_samples_record(&values, 0U) == 1);
    perf_samples_report("CASE", "cpu-compute", &values);

    read_words(argv[1], ntt_window, WINDOW_WORDS);
    read_words(argv[2], ntt_golden, HPU_NTT_REFERENCE_N);
    assert(hpu_ntt_reference_prepare(&ntt_reference, ntt_window + INPUT_WORD,
                                     ntt_window + FACTOR_WORD, 50061313U) == 0);
    hpu_ntt_reference_run(&ntt_reference);
    assert(memcmp(ntt_reference.output, ntt_golden, sizeof(ntt_golden)) == 0);

    read_words(argv[3], intt_window, WINDOW_WORDS);
    read_words(argv[4], intt_golden, HPU_NTT_REFERENCE_N);
    assert(hpu_intt_reference_prepare(&intt_reference, intt_window + INPUT_WORD,
                                      intt_window + FACTOR_WORD, 50061313U) == 0);
    hpu_intt_reference_run(&intt_reference);
    assert(memcmp(intt_reference.output, intt_golden, sizeof(intt_golden)) == 0);
    return 0;
}
'''


class PerfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="perf-host-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        (folder / "klib.h").write_text("#include <stdio.h>\n", encoding="utf-8")
        (folder / "harness.c").write_text(HARNESS, encoding="utf-8")
        cls.executable = folder / "perf-test"
        subprocess.run(
            shlex.split(os.environ.get("HOST_CC", "cc")) + [
                "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-DHPU_PERF_HOST_TEST", "-DHPU_LOG_LEVEL=1",
                "-I", str(folder), "-I", str(ROOT / "include"),
                str(ROOT / "runtime/it_perf.c"),
                str(ROOT / "runtime/it_ntt_reference.c"),
                str(folder / "harness.c"),
                "-o", str(cls.executable),
            ],
            check=True,
        )

    def test_sampling_statistics_and_report(self):
        result = subprocess.run(
            [str(self.executable), str(NTT_WINDOW), str(NTT_GOLDEN),
             str(INTT_WINDOW), str(INTT_GOLDEN)],
            check=True, capture_output=True, text=True
        )
        self.assertIn("metric=cpu-compute round=0 cycles=100", result.stdout)
        self.assertIn("rounds=3 average=200 minimum=100 maximum=300 spread=200",
                      result.stdout)


if __name__ == "__main__":
    unittest.main()
