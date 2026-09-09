# 05 CPU—HPU 结构连接测试

本章把“软件闭环自检”与“结构时序覆盖”分开验收。软件 `return 0` / `[HPU][PASS]` 只表示当前程序检查通过；不能据此认定 ready 反压、CDC、命令缓存满载或提交顺序已经验证。

逻辑计算/控制通道仍称 `custom0` 或 `cmd_kind=0`，实际 RISC-V 主 opcode 已是 `0x5B`；DLOAD/DSTORE 的 `custom1` 仍为 `0x2B`。发令使用当前 inline-asm 交付生成的机器码和 AM 接收层映射，不在用例里手工拼编码。

## 四个软件闭环入口

| 文件 | 实际程序 | 软件自检 | 仍需 IT 提供的证据 |
| --- | --- | --- | --- |
| `01_connection_stress/HPU_IT_DIR_STR_001.c` | 两个 profile；p0/A 与 p1/B 各自 DLOAD/DSTORE 回环，末尾一次 PSYNC | 两组各4096项结果、只读输入与全窗口非输出区 | 两通道无等待/单拍/多拍 ready 反压，等待期间 payload 稳定，跨域无丢失、重复或串配 |
| `01_connection_stress/HPU_IT_DIR_STR_002.c` | 模表 DLOAD → 连续9条 PMODLD → PFREE模表 → PSYNC | 最终完成、清除完成电平、状态无fault、全部DDR不变 | 实际缓存占用达到8、第9条受控等待、释放后恢复接收且保序 |
| `01_connection_stress/HPU_IT_STING_STR_001.c` | 确定性两轮：p0/A→OUT，再p1/B→OUT_B；每轮独立末尾一次PSYNC | 逐项结果和非输出窗口保护 | 外部STING随机驱动、合法指令序列/replay seed、ready/CDC监视。该C文件本身不是随机生成器 |
| `02_instruction_coordination/HPU_IT_DIR_STR_006.c` | 模表DLOAD → 8次“PMODLD + 128步CPU xorshift” → PFREE模表 → PSYNC | CPU末值`0xba61d264`、模表及全部DDR不变 | 普通RISC-V与HPU命令交错时的实际满载、接受/提交顺序及恢复 |

其它分支预测、核内异常、特权进入/返回测试不因上述四项通过而自动覆盖；是否具有可运行入口以其源码说明、`INDEX.tsv` 和 `NOT_QUALIFIED.tsv` 为准。

## 数据和结束方式

每轮先调用仅处理数据的 `v2_prepare`。profile 0 使用固定 producer A/B；profile 1 使用 AM 明确派生的0、1、q−1等边界输入，不冒充随机数据。用例在 `main` 中逐项写入并读回 BASE/SIZE，再 COMMIT；辅助接口不隐藏 CSR 配置或额外 HPU 指令。

DDR窗口为512 line，每line为256字节。A/B各64 line（4096个32位系数），模表在line192。STR001允许写回line128..191和256..319；STING确定性样例每轮只允许相应的一个输出区。STR002/STR006不允许任何HPU DDR写回。

期望值取自独立CPU影子，不以可能已被HPU污染的现场输入作golden。结束后失效cache再检查所有非输出区域，包括输入、模表和guard。这个检查能发现本窗口的意外改写，不能证明窗口外或额外读取AXI事务不存在；后者仍由monitor验收。

每段程序只在末尾发送一次PSYNC，随后依次调用 `wait_irq()`、`completion_clear()`、`check_status()`。这里的 `wait_irq()` 是读取MMIO完成电平，不是进入CPU中断处理函数；这些等待接口不再发PSYNC。DSTORE释放它写回的对象；只有仍驻留的模表对象再显式PFREE，避免重复释放。

## 队列/反压实验要点

- 先确认本包和simv采用兼容编码，并记录各自提交版本。
- 对被测九条PMODLD单独编号，区分前导DLOAD及后续PFREE/PSYNC。环境应控制队列排空/ready时序，让前八条确实积压，再观察第九条等待；不能仅根据CPU执行了九次函数调用推断队列曾满。
- 挂起期间检查请求payload保持稳定，恢复后检查每条命令恰好接受一次、无串配且保序。分别记录无等待、单拍和多拍反压的波形/覆盖点。
- STR006中的CPU计算会自然改变发令间隔；是否仍处于满载必须看真实占用，而不是由用例名推断。
- 运行日志记录 `PREPARE`、`ISSUE`、`CHECK` 及软件结果。发令突发内部没有成功路径printf；失败时打印具体phase/参数和一次失败后MMIO现场，避免串口输出人为排空队列。
- 轮询预算不是VCS总cycle预算。两轮数据准备、cache维护、全窗guard扫描和串口日志都消耗CPU周期；按实际运行日志配置仿真上限，不统一承诺100万cycle足够。

`STING_STR_001`只能作为确定性接入样例，不能以其两轮通过替代随机用例规模、种子重放或约束覆盖报告。任何结构时序验收都应同时保存IT monitor证据。
