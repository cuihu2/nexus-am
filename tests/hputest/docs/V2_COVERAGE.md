# v2 测试点落实范围与运行说明

依据 `HPU-IT测试点分解v2.xlsx` 的原用例 ID，以及本轮提供的功能、边界、性能、CPU 回归要求更新。
**本次不是“全部测试点已完成”或“全部 IT 已通过”。** 49 个后续用例中，29 个具备真实软件自检；
20 个尚未接入，只保留原因和源码，不在下载包发布占位二进制。具体缺口见 `../blocked.tsv`。
部分软件自检仅覆盖一个基础参数组合，不能等同整个测试点全部覆盖。
`00_bringup` 源码及RTL未更改；inline-asm已同步main `6903096`，嵌入数据按新物理ABI重新生成。
本版同步说明见 [inline-main更新](INLINE_MAIN_6903096.md)。

耗时优化、可选单子项执行、阶段cycle与全量UART结果导出见
[运行诊断说明](RUNTIME_UART_DIAGNOSTICS.md)。默认仍执行原有所有round和精确自检。

## 1. 统一读法与同步

每个 `.c` 保留显式 `main()`：准备数据、按地址逐项配置/读回 CSR、COMMIT、发出各条命令、等待、比较。
机械操作拆在小头文件和 runtime 中，不能用一个初始化/场景总函数隐藏整例。
长算子保留 producer 生成的完整 C 指令程序，并在发令前打印全部 DMA 绑定；实际指令文件随 provenance 下载。

用户已确认统一闭环为：

```text
数据/DLOAD → 必要配置和被测指令 → DSTORE → 释放剩余对象 → 唯一末尾 PSYNC → 完成等待 → 自检
```

连续命令依赖由硬件按编程手册处理，不在模表加载后、每个 stage 或 DSTORE 前增加 PSYNC。
多个独立 round 各自末尾一次，必须等上一 round 完成、清除事件后才能开始下一 round。
00 的纯 MMIO DMA 用例仍保持无 PSYNC，不能按上述算子流程改写其测试意图。

日志包含 START、ROUND/阶段、对象、地址/line、长度、q、实际状态、首个错误系数及 PASS/FAIL。
命令压力段和正常轮询内部不打印；ISR 不做 printf。失败后的多寄存器快照不是原子快照。
`main()==0` 只证明本 C 中的检查通过；编译成功、UART 打印、软件 PASS 都不替代波形/monitor 验收。

## 2. 03：九项基础指令

路径：`src/03_compute_instructions/`。下表数字指 `HPU_IT_DIR_INS_C0_XXX` 后缀，不建立第二套编号。

| ID | 被测指令 | 本轮实际组合 |
| --- | --- | --- |
| 001 | PADD | 4轮：独立目的、覆盖源0/源1、连续依赖；模回绕 |
| 002 | PSUB | 4轮：独立目的、覆盖源0/源1、连续依赖；借位/回绕 |
| 003 | PMUL | 6轮：对象模式、立即数7、目的覆盖、立即数0/1/255 |
| 004 | PMAC | 4轮：初值0、初值q-1连续累加、立即数255、目的/源同为p0 |
| 005 | PNTT | 2种数据 × stage0/1/11，dst/src/twiddle为p2/p0/p1或p0/p2/p3 |
| 006 | PINTT | 同上，逆向stage和lazy-scale twiddle，独立物理单stage C模型 |
| 007 | PMODLD | 2种数据，各自q0→q1→q0；三个PMUL输出验证上下文切换 |
| 008 | PSYNC | 空闲、DMA、计算三种程序，各两轮；真PLIC中断、rearm、清除后检查空闲 |
| 009 | PFREE | p0/p7合法释放后复用，同一对象从A换为B，再写回检查 |

005/006不再占位，也不再散落到另一下载包。所有9例合计40段完整程序。
单stage输入是原始系数数组，不把“全NTT后结果”充作单stage golden。
本轮选择0/1/11三个stage，不宣称所有stage、所有对象组合或驻留资源上限都已覆盖。
008是否确实在DMA/计算尚忙时提交PSYNC，需要对应时刻的monitor证据。

基础数据来自固定 producer 的两组4096×uint32输入；边界profile由AM显式派生0、1、q-1、q-2组合。
它们不是上游原始fixture，不应在报告中混淆。普通用例窗口512line（128KiB），软件影子位于HPU窗口外。
只允许显式DSTORE目标被写，其余输入、模表、twiddle和guard全部与不可变影子比较；golden不得读取可能已被HPU污染的输入DDR。
这只能证明被检查窗口的内容不变，不能证明窗口外没有AXI写入或相同值重复写入。

## 3. 04：算子与算法库

| 用例后缀 | 本轮实现 | 未覆盖/尚缺 |
| --- | --- | --- |
| CMB_001 | Q4→P3 FastBConv，N4096；93指令/40DMA；4个normalized Q及3个P分量逐项检查 | P→Q、边界数据、其它代表性规模 |
| CMB_002 | 整体NTT，Q0/N4096；pre-twist+12stage+写回；57指令/16DMA | 其它模数基、边界数据、其它规模 |
| CMB_003 | 整体INTT，Q0/N4096；12stage+归一化/逆twist+写回；57指令/16DMA | 同上 |
| CMB_004 | 未接入KeySwitch | 独立密钥/常量/scratch及DMA绑定未完成 |
| CMB_005 | 未接入NTT+Auto | producer已有Auto专用交付，AM接收校验/绑定尚未实现 |
| CMB_009..015 | 未接入真实算法库API | 需库仓库、固定commit、接口、参数、密钥、数据、精度和golden |
| STING_CMB_007 | 未接入随机长链 | STING入口、seed重放、对象分配与逐阶段golden |

整体NTT/INTT独立用640line窗口；BConv用2048line窗口，保留producer原始offset并添加尾部guard。
输入/常量从producer交付接收，输出区填poison，golden位于只读ELF中而不预填到HPU输出区。
NTT/INTT golden在构建时用独立数学变换复算；BConv按FastBConv公式复核，不错误替换为精确CRT还原。
每次DLOAD/DSTORE的序号、对象、line及来源记录在 `resolved_dma.tsv`；原C/ASM/inst32/cmd26及表格均保留。
接收前还使用固定inline-asm源码本次重新生成的编码表逐条核对机器码，防止几个旧文件彼此一致却都编码错误。

原CMB_009把单条PADD称作Poseidon HADD，不符合本轮“以算法库接口为边界”的要求。
已取消该假接口覆盖，PADD功能仍由03-001保留。encode/bootstrapping是原表条目，保留ID，不能静默删除。

## 4. 配置、地址、资源和数据边界

01、02全部13个现有用例已改为显式阶段、短CSR名、逐失败现场和不可变影子/guard检查。
逐项范围见两个目录的README；以下缺口不得在测试报告中写成已覆盖。

| 要求 | 当前落点 | 还需要的验收 |
| --- | --- | --- |
| 配置写读、COMMIT前后、重复提交 | CFG001/002、STING_CFG001 | BASE/SIZE混写及提交原子生效需monitor；只读回shadow不足以证明 |
| 首/中/末地址、1line/多line | CFG003：首→末1line、中间3line、末端64line | 地址换算/突发长度的AXI波形；更大合法窗口 |
| 窗口越界、fault及W1C | CFG005：已定义的越界DLOAD；故障出现和清除都有超时 | 其它故障码/方向、未提交、length0等须按明确接口补齐，不能由现象猜语义 |
| 对象释放复用与对象编号 | PATH002、INS009；包括逻辑p7 | p7不代表容量压力；5个普通bank和1个小bank不等于8个可同时驻留大对象 |
| 0/1/q-1、借位、回绕、立即数 | 03的边界profile/模式组合 | 更多模数/长度/源目的全组合 |
| 连续完成事件、清除重用 | CFG006、INS008两轮 | 丢/重复中断和提前完成须monitor结合数据检查 |

不支持或未定义的输入不设“期望某fault”的猜测判据。新增非法输入前，需提供编程手册的具体条款。

## 5. 结构连接

05的4个可执行样例已补日志和全窗口保护：双对象回环、九条连续PMODLD、两个确定性STING载荷、CPU整数/访存交错。
`STR002`连续发9条不等于队列已满，必须外部阻塞消费端并记录occupancy=8、第9条等待及恢复顺序。
无等待/单拍/多拍ready、payload稳定、错误路径无副作用、异常/特权进入返回均不能由普通C返回0单独证明。
三个未绑定样例明确报出所需事件注入/参考模型，见05 README与blocked清单。
物理opcode是0x5B/0x2B，旧表的custom0指内部cmd_kind=0，不再使用物理0x0B。

## 6. 性能、完整应用、CPU回归

这三部分未完成，不输出伪cycle、伪speedup或算法替身PASS。

- 性能6项：NTT、INTT、BConv、KeySwitch、密文乘法、RelinOnly保留原ID。需固定同输入/参数/精度、CPU基线、统一计数入口、纯计算/端到端边界及轮数；逐轮先验结果再报告cycle、均值/最小/最大/波动，按CPU/HPU计算speedup。CPU从发令到PSYNC的时间不能标成HPU纯计算时间。
- APP001：当前表仅一个应用项，用户新增了A*B+C和向量规约两种要求；还需确认真实FHE密文语义及库接口/密钥/精度。当前两者均未实现，不能以普通整数乘加代替FHE应用。
- CPU回归：本次没有运行Difftest。现有CPU构建job保留不动；需集成前测试集/配置/参考模型/通过基线和运行入口。定向/随机/特权/异常/中断/访存/原子/系统工作负载均需真实执行并比较；新增失败归因，既有失败保留证据，经评审才豁免。

## 7. 下载和复验

GitHub Actions产物统一名 `nexus-am-hpu-workloads`，内部是00至07章节目录和 `INDEX.tsv`。
只发布40组非占位ELF/BIN/反汇编（含00的11组辅助程序）；20项未就绪只列原因，不夹在可运行列表中。
`provenance/`保留固定producer版本、实际指令、DMA/数据布局和本说明。
新用例需要更多初始化、逐项golden和guard扫描，不承诺100万cycle一定足够；由IT测定预算并保存原始超时日志。
失败回传至少包含case ID、AM/producer/RTL/simv版本、cycle-limit、最后阶段、首错日志和对应波形。
