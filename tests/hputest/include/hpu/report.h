#ifndef HPU_REPORT_H
#define HPU_REPORT_H

#include <stdint.h>

/*
 * UART 结果诊断：必须由 main 在每轮开始时明确设置用例标识与轮次。
 * 名称允许可打印 ASCII 字符，但不能包含空白或逗号；字符串须保持有效。
 * 同一轮多次比较通过自动递增的 block 区分，不猜测输出所属的用例。
 */
int result_context(const char *case_id, unsigned round);

/*
 * 调用方先完成 HPU 同步和输出 cache invalidate；本函数不发 HPU 指令，
 * 也不额外维护 cache。actual 每个位置只读取一次，完整比较之后才返回。
 * q=0 为普通 32 bit 数据；q!=0 时要求 actual 与 golden 精确相等且 <q。
 * 模 q 同余但非规范的结果仍失败，不能用“误差容限”掩盖整数计算错误。
 *
 * 默认输出摘要、前 4 项和最多 8 个错误；HPU_DUMP_RESULTS=1 输出每项。
 * 全量日志可由 scripts/parse-uart-results.py 导出 CSV；不在 ISR/轮询中调用。
 */
int result_compare(const char *phase, volatile const uint32_t *actual,
                   const uint32_t *golden, unsigned words, uint32_t q);

#endif
