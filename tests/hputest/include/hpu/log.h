#ifndef HPU_LOG_H
#define HPU_LOG_H

/* 先读取 AM 的 printf 声明及 printf_ 别名，日志开关不修改公共库接口。 */
#include <klib.h>

#ifndef HPU_LOG_LEVEL
#define HPU_LOG_LEVEL 1
#endif
#ifndef HPU_DUMP_RESULTS
#define HPU_DUMP_RESULTS 0
#endif
#if HPU_LOG_LEVEL < 0 || HPU_LOG_LEVEL > 2
#error "HPU_LOG_LEVEL must be 0 (silent), 1 (minimal), or 2 (verbose)"
#endif
#if HPU_DUMP_RESULTS != 0 && HPU_DUMP_RESULTS != 1
#error "HPU_DUMP_RESULTS must be 0 or 1"
#endif
#if HPU_DUMP_RESULTS && HPU_LOG_LEVEL != 2
#error "HPU_DUMP_RESULTS=1 requires HPU_LOG_LEVEL=2"
#endif

/*
 * 关闭的日志不求值实参：寄存器读取、计数递增等测试步骤须先单独执行。
 * level 1 仅保留用例开始/结束及错误；阶段、正常读回值属于 level 2。
 */
#if HPU_LOG_LEVEL >= 1
#define LOG_ERROR(...) ((void)printf(__VA_ARGS__))
#define LOG_EVENT(...) ((void)printf(__VA_ARGS__))
#else
#define LOG_ERROR(...) do { if (0) (void)printf(__VA_ARGS__); } while (0)
#define LOG_EVENT(...) do { if (0) (void)printf(__VA_ARGS__); } while (0)
#endif
#if HPU_LOG_LEVEL == 2
#define LOG_DEBUG(...) ((void)printf(__VA_ARGS__))
#else
#define LOG_DEBUG(...) do { if (0) (void)printf(__VA_ARGS__); } while (0)
#endif

#endif
