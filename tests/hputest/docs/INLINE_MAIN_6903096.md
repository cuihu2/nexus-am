# 同步 inline-asm main：6903096

生产者固定为 `69030963e71dbcf32897e8ae08695cfa2e65d79a`；上一固定版本为`b405f2a`。
纳入上游`159e483`（原生custom2）和`6903096`（NTT数据/执行模型修正）。
仅更新AM引用、数据接收、用例与验证；未修改inline-asm源码或任何RTL。

## 必须同时更新的内容

| 项目 | 本版要求 |
| --- | --- |
| HPU opcode | producer直接输出0x5B/0x2B；接收端不再修改word，拒绝旧0x0B |
| STG接口 | `pntt/pintt dst,src,twiddle,stage,mode,flag`，目的与源明确分离 |
| 完整NTT/INTT | p0/p3交替，各stage后释放旧源；57条指令、16次DMA、一次末尾PSYNC |
| 系数域数组 | 每RNS分量按bit_reverse物理顺序，包含BConv输入/结果 |
| NTT域数组 | 按forward_layout物理顺序，包含MM输入/结果 |
| PNTT | 按loader batch/lane执行蝶形，再P网络排列 |
| PINTT | k对应forward stage `log2(N)-1-k`，先P⁻¹，使用lazy-scale表 |
| pre/post因子 | pre为`psi^bit_reverse(p)`；post为`N^-1*psi^-bit_reverse(p)` |

不能把所有uint32数组一律bit-reverse，也不能把新表套进旧group-major黄金模型。
MM仍是逐项模乘，但其输入域是NTT；接收时除pointwise校验外，还验证自然序数学数组到forward_layout的映射。
BConv仍是FastBConv Q4→P3，93指令/40DMA；先独立算自然序数学golden，再逐分量映射到物理序，不能改用精确CRT替代。

## 03/04变换用例

03-005/006仍各执行2种数据×stage0/1/11，不增减子项。源/目的组合为`dst2,src0,twiddle1`
和`dst0,src2,twiddle3`；DSTORE释放目的，其后只释放仍驻留的源、twiddle和模表。
单stage C模型直接消费物理测试向量，模拟loader和P/P⁻¹；不把整变换golden冒充单stage结果。
04-002/003直接调用新producer完整程序，独立数学NTT/INTT golden映射到物理域比较。

保留完整guard、失败返回1、UART摘要/全量结果、阶段cycle和`subcase=N`选择。
UART中的index为物理下标，后处理不能直接当自然序系数索引；原始数学数据和映射说明都在provenance。
00源码和指令流程不变，但嵌入的数据及版本已重新生成，因此不能期待BIN逐字节等于旧版本。

## 验证与复测边界

构建包含原生opcode/三对象位段固定向量、旧语法拒绝、数据域和表格负例、真实C单stage模型与冻结RTL dump对照、
独立DFT/完整变换数学对照，以及标准/全量UART两种产物校验。冻结dump和host测试都不等于当前集成环境已跑通。
新ELF需要匹配该编程手册语义的RTL/simv；应记录AM、producer和RTL三者版本后复测。
未接入的算法库/性能/应用项仍未接入，不因更新submodule变成PASS。
