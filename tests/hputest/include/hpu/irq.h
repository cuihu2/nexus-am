#ifndef HPU_IRQ_H
#define HPU_IRQ_H

/*
 * HPU 完成中断固定接到 PLIC source 257、S-mode context 1。
 * 用例在发 PSYNC 前调用 irq_open()，再用 irq_wait() 等待处理函数。
 */
int irq_open(void);
int irq_wait(void);
void irq_close(void);

#endif
