/* 仅host运行时单测使用；保持关闭日志不求值的行为，不替代目标UART验证。 */
#include <stdio.h>
#define LOG_EVENT(...) do { if (0) (void)printf(__VA_ARGS__); } while (0)
#define LOG_ERROR(...) do { if (0) (void)printf(__VA_ARGS__); } while (0)
