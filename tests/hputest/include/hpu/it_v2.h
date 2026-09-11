#ifndef HPU_IT_V2_H
#define HPU_IT_V2_H

#include <hpu/steps.h>

/*
 * 后续章节的数据与指令小接口；不改变已跑通的 00 冒烟测试。
 * main 仍需显式配置 CSR、发出命令，并在整段程序末尾执行一次 PSYNC。
 */

/*
 * 只准备数据：profile=0 使用 producer A/B 并规范到 q0；profile=1 使用
 * AM 明确派生的边界输入，不冒充 producer 原始向量。模上下文 0/6 分别为
 * q0/q1。其余 DDR 全部填充 guard；没有任何区域自动获得 HPU 写权限。
 */
int v2_prepare(unsigned profile, uint32_t q0, uint32_t q1);

/*
 * 期望值来自 CPU 独立影子数组，不能用可能被 HPU 污染的 DDR 输入作 golden。
 * 地址越界或尚未 prepare 时返回 NULL；调用方不得修改返回的数据。
 */
const uint32_t *v2_expected(unsigned line);

/* 同步修改 DDR 和影子；仅用于发出本轮 HPU 命令之前的数据准备。 */
int v2_fill(unsigned line, uint32_t value, unsigned words);
int v2_copy(unsigned line, const uint32_t *data, unsigned words);

/* 按整行显式许可输出，其余所有窗口位置都必须保持原值。 */
int v2_allow_output(unsigned line, unsigned lines);
int v2_check_memory(const char *phase);

/* 逐项精确比较；q=0 表示非模数数据，否则还要求结果处于 [0,q)。 */
int v2_check_words(const char *phase, unsigned line,
                   const uint32_t *golden, unsigned words, uint32_t q);

/*
 * 每次调用只发一条 producer 生成的指令，不隐含配置、DLOAD 或 PSYNC。
 * 对象组合支持 (dst,a,b)：(2,0,1)、(0,0,1)、(1,0,1)、(2,1,0)、
 * (7,0,1)、(2,2,0)、(2,2,1)；不支持的参数会报错，绝不静默替换操作数。
 */
int op_add(unsigned dst, unsigned a, unsigned b);
int op_sub(unsigned dst, unsigned a, unsigned b);
int op_mul(unsigned dst, unsigned a, unsigned b);
int op_mac(unsigned dst, unsigned a, unsigned b);

/* 立即数模式：dst/a 为 2/0 或 0/0，imm 为 0、1、7、255。 */
int op_mul_imm(unsigned dst, unsigned a, unsigned imm);
int op_mac_imm(unsigned dst, unsigned a, unsigned imm);

/*
 * 单 stage 为显式三对象 out-of-place 操作：dst/src/twiddle 支持
 * p2/p0/p1 或 p0/p2/p3，stage 为 0..11。调用方保证 dst 空闲、两个源 live；
 * 本接口不隐含释放，源对象由调用方 PFREE，目标在 DSTORE 后释放。
 */
int op_ntt(unsigned dst, unsigned src, unsigned twiddle, unsigned stage);
int op_intt(unsigned dst, unsigned src, unsigned twiddle, unsigned stage);

#endif
