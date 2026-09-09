# 01 配置与状态测试

每个测试点保持独立 C 文件，`main` 中直接写 BASE/SIZE、读回并 COMMIT。
串口日志按数据准备、配置、提交、发令、完成、自检分段。失败报告源文件行号、
阶段和 STATUS/FAULT/IRQ/BASE/SIZE 的实际读值；这些寄存器分别采样，不能视为
同一时刻的原子快照，失败诊断也不会读取有 claim 副作用的 PLIC 寄存器。

| 用例 | 本次实际软件检查 | 仍需外部证据 |
| --- | --- | --- |
| `HPU_IT_DIR_CFG_001` | 四个 shadow CSR 写入/读回、COMMIT 后窗口有效、空闲 PSYNC 完成、IRQ W1C 清除 | CSR 访问握手及逐周期状态，`needs-monitor` |
| `HPU_IT_DIR_CFG_002` | 先提交 A 窗口、再提交 B 窗口，B 相对地址 `0→64` 对应物理 line `256→320` 的一行回环；B canary 为 A 首行逐项 `+1 mod q`，保证读错旧窗口不能通过 | COMMIT 前后原子生效、无 BASE/SIZE 半更新组合，`needs-monitor` |
| `HPU_IT_DIR_CFG_003` | 三段独立程序：首地址到末行的 1 line、窗口中间 3 lines、末端合法 64 lines；各自逐字比较 | AXI 地址/长度、窗口外副作用，`needs-monitor` |
| `HPU_IT_STING_CFG_001` | 固定偏移序列和两种 BASE/SIZE 写序的 shadow 读回/提交 | STING 随机间隔、回放、覆盖率和配置原子性；现有 `seed_tag` 不是已接入的随机激励 |
| `HPU_IT_DIR_CFG_004` | DLOAD/DSTORE 程序中实际采样到 BUSY，再观察最终 IRQ 与空闲，比较完整 4096-word 回环 | 更细的逐周期状态迁移，`needs-monitor` |
| `HPU_IT_DIR_CFG_005` | 向 `[0,512)` 窗口的 line 512 发出一行 DLOAD，检查 load/p0 fault 记录及 FAULT W1C；不允许任何输出 | 非法请求未发出 AXI 事务、窗口外地址未受影响，`needs-monitor` |
| `HPU_IT_DIR_CFG_006` | 两段独立空闲程序，各末尾一次 PSYNC，每次观察并清除 MMIO 完成电平 | 真实 PLIC/CPU 中断、精确中断次数，`needs-monitor` |

## 数据和通过条件

- 使用 inline-asm producer 的两组 4096 个系数；CPU 独立 shadow 保存输入和期望值。
- `v2_prepare` 先给 512-line 测试区填充地址相关 guard，输出保留 poison，不预装 golden。
- 仅将明确的 DSTORE 目标标记为允许写入，最后检查其余窗口中的输入、模表和 guard。
- 窗口回环、数据检查、完成事件检查均通过才 `return 0`。日志中的
  `SW-CHECK-PASS` 只代表软件检查通过，不意味着 `needs-monitor` 部分已经验收。
- CFG004 有意要求软件观察到 BUSY；日志会记录最后的 STATUS、是否曾见 BUSY 和
  轮询次数。`TIMEOUT` 是轮询上限，不是 VCS cycle 数，不能据此保证一百万周期足够。

## 尚未覆盖的边界

当前没有把“未提交配置”“零长度”“非法对象/不支持类型”等都当作同一种故障。
这些场景需按明确的接口定义逐项增加预期和激励，不能从仿真现象反推语义。
同值重复 COMMIT、部分 shadow 写入时的原子性、DSTORE 故障方向等也没有被
上表现有软件检查替代。guard 只能验证本测试窗口中的最终内容，不能证明所有
DDR 地址、读事务或瞬时 AXI 副作用不存在。
