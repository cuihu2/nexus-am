#ifndef HPU_PROGRESS_H
#define HPU_PROGRESS_H

/*
 * 每个 main 最开始调用一次，且必须在 irq_open/_cte_init 降到 S 模式之前。
 * 空 mainargs 或 all 运行完整用例；subcase=N 只运行编号 N 的子场景。
 * 返回 1 表示参数无效，调用者应返回 case_fail，不能把未运行视为通过。
 * 仅 HPU_LOG_LEVEL=2 时设置 cycle 访问并计时，不开启计时器或任何中断；
 * 默认最小日志及静默版本只解析子项选择，不读 cycle、不写 mcounteren。
 */
int progress_begin(const char *case_id, unsigned subcases);

/* 未初始化或编号越界时返回 0；选择子场景不改变场景内部的轮数/判据。 */
int subcase_selected(unsigned index);

/*
 * 标记新阶段开始，打印上一阶段消耗的 CPU cycle，不是 HPU 纯计算 cycle。
 * 本次日志打印完成后重新取起点，使下次 elapsed 不包含本次 phase 日志。
 * 阶段内部的其他 printf、CPU 工作和等待仍属于该阶段的 CPU cycle。
 */
void phase_mark(const char *phase);

#endif
