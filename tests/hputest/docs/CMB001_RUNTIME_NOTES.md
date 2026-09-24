# CMB001 慢在哪里：源码能确认的部分

基线：Nexus-AM `543abe84085e7c0fadc9445e7124983a07a1b380`。
这次只增加应用用例，不修改 CMB001 的指令、golden、日志或同步行为。

`HPU_IT_DIR_CMB_001.c` 只调用一次 `hpu_program_bconv()`，没有 profile/round
外层循环；`progress_begin(..., 1U)` 也是一轮。Q4→P3 的归一化/累加是同一 BConv
算子的内部必要步骤，不是4组或7组独立 testcase 串行。

| 阶段 | 当前实际工作量 |
|---|---|
| 准备窗口 | 2048 lines ×256 B =512 KiB，CPU复制131072个 uint32 |
| 清理输入 cache | 8192个64 B cache block |
| HPU 程序 | 93条：40 DMA、7 PMUL、9 PMAC、7 PMODLD、29 PFREE、1 PSYNC |
| DMA | 33次 DLOAD、7次 DSTORE；共2497 lines =639232 B |
| 结果比较 | 4个Q归一化结果 +3个P最终结果，共28672个 uint32 |
| 非写入区/guard检查 | 102400个 uint32 |
| 输出与guard的cache失效 | 合计8192个64 B cache block |

目标端不重新跑 FastBConv 软件 golden；golden 在主机生成后嵌入 ELF。
默认 minimal/silent 也没有逐系数/逐DMA打印：verbose 才有详细诊断，full才全量打印。
因此仅由“CMB001很慢”不能判定是重复轮次，更不能直接判定 HPU死锁或算力问题。

## 如何定位当前这次运行

先记录下载包的 `revision`、`inline_asm_commit`、`log_mode`、`uart_results`、
模拟周期数和最后输出/PC。用当前已有的 verbose 单例（不启用全量dump）分辨：

- `prepare`：CPU复制与cache clean，HPU尚未发指令。
- `issue`：CPU发命令及硬件 ready 反压/串行数据依赖，可能已经发生 DMA/计算。
- `wait-completion`：看 STATUS/FAULT/IRQ、DMA ack 及 HPU执行进度。
- `compare-results` / `check-guard`：主要是CPU读回、自检与cache维护。

百万模拟周期和几小时墙钟时间不是同一指标。应比较各阶段模拟 cycle，
以及仿真器每秒推进多少 cycle；FGP加速效果也不能仅由一个总耗时反推。
缺少当前这次的日志/PC/波形，不能在上述阶段中确定唯一根因。
