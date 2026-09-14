#include <hpu/log.h>
#include <stdarg.h>

#if HPU_LOG_LEVEL == 0
/*
 * 静默 ELF 的链接兜底：屏蔽公共 AM 库的输出和 UART 初始化。
 * 不调用格式化器、不遍历字符串、不访问 UART MMIO；不改 _halt 返回码。
 * 用例本身仍须使用 log.h，避免计算仅供日志使用的昂贵实参。
 */
int __wrap_printf_(const char *format, ...) { (void)format; return 0; }
int __wrap_printf(const char *format, ...) { (void)format; return 0; }
int __wrap_atomic_printf_(const char *format, ...) { (void)format; return 0; }
int __wrap_vprintf_(const char *format, va_list args) {
    (void)format; (void)args; return 0;
}
int __wrap_vprintf(const char *format, va_list args) {
    (void)format; (void)args; return 0;
}
int __wrap_puts(const char *text) { (void)text; return 0; }
int __wrap_putchar(int character) { return character; }
void __wrap__putc(char character) { (void)character; }
void __wrap__putchar(char character) { (void)character; }
void __wrap___am_uartlite_putchar(char character) { (void)character; }
void __wrap___am_16550_putchar(char character) { (void)character; }
void __wrap___am_init_uartlite(void) {}
void __wrap___am_init_16550(void) {}
#endif
