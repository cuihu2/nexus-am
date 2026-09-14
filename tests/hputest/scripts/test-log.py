#!/usr/bin/env python3
"""验证日志编译门禁、静默链接兜底以及与 PASS/FAIL 返回值的隔离。"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
WRAPS = (
    "printf_", "printf", "atomic_printf_", "vprintf_", "vprintf",
    "puts", "putchar", "_putc", "_putchar", "__am_uartlite_putchar",
    "__am_16550_putchar", "__am_init_uartlite", "__am_init_16550",
)
MACROS = r'''
#include <assert.h>
#include <hpu/log.h>
#include <hpu/result.h>
int main(void) {
    int count = 0;
    LOG_DEBUG("debug=%d\n", ++count);
    LOG_EVENT("event=%d\n", ++count);
    LOG_ERROR("error=%d\n", ++count);
    assert(count == (HPU_LOG_LEVEL == 2 ? 3 : HPU_LOG_LEVEL == 1 ? 2 : 0));
    case_start("test");
    assert(case_pass("test") == 0);
    assert(case_fail("test", 7U) == 1);
    case_not_qualified("test");
    case_expected_failure("test");
    case_hold("test");
    return 0;
}
'''
WRAPPER_CALLS = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
/* 不引入 glibc 的 putchar/vprintf 内联重定向，直接验证 AM 链接符号。 */
int printf(const char *, ...);
int puts(const char *);
int putchar(int);
int vprintf(const char *, va_list);
int printf_(const char *, ...);
int atomic_printf_(const char *, ...);
int vprintf_(const char *, va_list);
void _putc(char);
void _putchar(char);
void __am_uartlite_putchar(char);
void __am_16550_putchar(char);
void __am_init_uartlite(void);
void __am_init_16550(void);
void _halt(int);
extern int halted;
static void test_va(const char *unused, ...) {
    va_list args;
    va_start(args, unused);
    /* 无效 format 验证静默兜底连格式串都不读，不只是不向 UART 发字符。 */
    const char *bad = (const char *)(uintptr_t)1U;
    assert(vprintf_(bad, args) == 0);
    assert(vprintf(bad, args) == 0);
    va_end(args);
}
int main(void) {
    const char *bad = (const char *)(uintptr_t)1U;
    assert(printf_(bad) == 0);
    assert(printf(bad) == 0);
    assert(atomic_printf_(bad) == 0);
    assert(puts(bad) == 0);
    assert(putchar('X') == 'X');
    _putc('X'); _putchar('X');
    __am_uartlite_putchar('X'); __am_16550_putchar('X');
    __am_init_uartlite(); __am_init_16550();
    test_va("unused");
    _halt(1); assert(halted == 1);
    _halt(0); assert(halted == 0);
    return 0;
}
'''
ORIGINALS = r'''
#include <stdarg.h>
#include <stdlib.h>
int printf_(const char *f, ...) { (void)f; _Exit(90); }
int printf(const char *f, ...) { (void)f; _Exit(90); }
int atomic_printf_(const char *f, ...) { (void)f; _Exit(90); }
int vprintf_(const char *f, va_list a) { (void)f; (void)a; _Exit(90); }
int vprintf(const char *f, va_list a) { (void)f; (void)a; _Exit(90); }
int puts(const char *s) { (void)s; _Exit(90); }
int putchar(int c) { (void)c; _Exit(90); }
void _putc(char c) { (void)c; _Exit(90); }
void _putchar(char c) { (void)c; _Exit(90); }
void __am_uartlite_putchar(char c) { (void)c; _Exit(90); }
void __am_16550_putchar(char c) { (void)c; _Exit(90); }
void __am_init_uartlite(void) { _Exit(90); }
void __am_init_16550(void) { _Exit(90); }
int halted = -1;
void _halt(int code) { halted = code; }
'''


class LogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="log-host-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        (cls.work / "klib.h").write_text("#include <stdio.h>\n")
        (cls.work / "macros.c").write_text(MACROS)
        (cls.work / "calls.c").write_text(WRAPPER_CALLS)
        (cls.work / "originals.c").write_text(ORIGINALS)
        cls.compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        cls.flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                     "-fno-builtin", f"-I{cls.work}", f"-I{ROOT / 'include'}"]

    def test_disabled_macros_do_not_evaluate_arguments_and_keep_return_codes(self):
        for level in (0, 1, 2):
            with self.subTest(level=level):
                binary = self.work / f"macros-{level}"
                subprocess.run(self.compiler + self.flags + [
                    f"-DHPU_LOG_LEVEL={level}", str(self.work / "macros.c"),
                    "-o", str(binary)], check=True, capture_output=True, text=True)
                result = subprocess.run([str(binary)], check=True,
                                        capture_output=True, text=True)
                if level == 0:
                    self.assertEqual(result.stdout, "")
                else:
                    self.assertIn("[HPU][PASS] test rc=0", result.stdout)
                    self.assertIn("[HPU][FAIL] test:7 rc=1", result.stdout)
                    self.assertEqual("debug=" in result.stdout, level == 2)

    def test_silent_wrappers_bypass_formatter_uart_and_do_not_wrap_halt(self):
        binary = self.work / "wrapped"
        subprocess.run(self.compiler + self.flags + [
            "-DHPU_LOG_LEVEL=0", str(self.work / "calls.c"),
            str(self.work / "originals.c"), str(ROOT / "runtime/it_log_silent.c"),
            *[f"-Wl,--wrap={symbol}" for symbol in WRAPS], "-o", str(binary),
        ], check=True, capture_output=True, text=True)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "")

    def test_bad_level_and_full_dump_in_quiet_modes_are_compile_errors(self):
        for level, dump in ((-1, 0), (3, 0), (0, 1), (1, 1), (2, 2)):
            with self.subTest(level=level, dump=dump):
                result = subprocess.run(self.compiler + self.flags + [
                    f"-DHPU_LOG_LEVEL={level}", f"-DHPU_DUMP_RESULTS={dump}",
                    "-fsyntax-only", str(self.work / "macros.c"),
                ], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("#error", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
