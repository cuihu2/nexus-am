# 2026-09-05 编程手册 0.4：用例同步更新

本次依据用户提供的 `HPU_PROGRAMMING_MANUAL.pdf`（22页，2026-09-05，v0.4）
第6.1、6.3、8.1节更新测试软件。2026-09-07 按用户确认，进一步采用该 PDF
第3.2节位段图及公式修正 STG 编码器和 AM 接收检查。该修正曾按用户要求
从原 `HPU_SEAL` 分支撤销，再放入独立试验分支 `HPU_SEAL_manual_0905`。
现在按用户要求改用上游 `main`，固定引用其“修复 ntt dma 的 encode 问题”提交
`04d1825bdbce4dc649a683a722e59cf100c1a686`。当前来源已不是试验分支；本次不修改
原 `HPU_SEAL` 或 `HPU_SEAL_manual_0905` 分支。
RTL 未修改，未进行 VCS 仿真。

## 明确落实的流程

模表加载后先执行 `PSYNC → 等待完成 → 清除通知`，再执行PMODLD。
等待过程中检查FAULT和STATUS，收到通知后重新确认窗口有效且空闲。
MMIO完成电平必须清到0并释放清除请求后，才能开始下一阶段。

- 08：AM接收脚本将producer MM函数拆成`mm_load_mod()`和`mm_compute()`，
  原有十条机器码及四笔x10/x11绑定保留。main在两者之间增加PSYNC。
  两次PSYNC均走PLIC；第一轮handler完成claim/HPU清除/complete后，
  `irq_rearm()`核对状态并重置完成标志，不重复执行CTE初始化。
- 09：相同两个程序阶段；通过MMIO轮询和清除第一轮事件，再等待计算阶段事件。
- PATH_001、STING_CMD_001、PATH_004、INS_C0_001/002/003/004/007、
  CMB_009、STR_002/006：main中的模表DLOAD与PMODLD之间增加同样同步。
- INS_C0_003：DSTORE完成并清除事件后才复用目的对象p2。
- INS_C0_009：DLOAD完成后再PFREE；PFREE完成后再复用相同对象号。

各阶段失败均返回1并保留UART定位。最终4096系数比较仍决定自检结果。
用例数量和ID不变。原始producer `mm.c/.inst32/.cmd26`随工件保留，
`mm_phases.c/h`明确属于AM派生适配文件，并非上游原始交付。

## 已确定的 STG 编码

用户已明确选择最新 PDF 第3.2节的位段图和公式，不再沿用其后文的旧编码示例：

```text
word = (OPC << 28) | (pdata << 25) | (pdata << 22)
     | (ptwid << 14) | (stage << 10) | (mode << 8) | (flag << 7) | 0x0B
```

`pdata` 同时进入目的对象和第一源对象字段，`ptwid` 零扩展后进入
bits[21:14]（有效对象号在 bits[16:14]）。例如：

| 指令 | inst32 | cmd26 |
| --- | --- | --- |
| `pntt p0,p3,15,0,0` | `0x4000FC0B` | `0x08001F8` |
| `pntt p2,p3,15,0,0` | `0x4480FC0B` | `0x08901F8` |
| `pintt p0,p3,15,0,0` | `0x5000FC0B` | `0x0A001F8` |
| `pintt p2,p3,15,0,0` | `0x5480FC0B` | `0x0A901F8` |

编码由固定的上游 `main` 版本生成，STG 字段与此前试验分支一致，AM 不手工替换
程序中的机器码。位段核对不代表所有手册章节和端到端程序契约都已验证。
NTT/INTT 用例在 AM 中仍缺少完整程序、数据、golden 和 relocation 的端到端接入，
因此继续保持未就绪状态；原因不再是未选择 STG 公式。编码检查通过也不等于
NTT 功能已在 IT 环境通过。

## 当前上游 DMA / cmd26 修正

这次切换不仅更新分支名：上游已改动 DMA 的 32-bit 编码，AM 的独立预期值、
导入检查和产物检查随之更新；简单用例也通过同一编码器重新生成单指令头文件。

```text
inst32 = (rs2 << 27) | (rs1 << 22) | (flag << 17) | (obj << 10)
       | (operation << 8) | (dir << 7) | 0x2B
DLOAD: dir=0, operation=type
DSTORE: dir=1, operation=rel << 1
custom0 cmd26 = inst32 >> 7
custom1 cmd26 = (1 << 25) | (inst32 >> 7)
```

旧 DMA 位段和丢弃 GPR 编号后重排的 cmd26 都不再适用。MM 的模表 DLOAD、
输入 A DLOAD、输入 B DLOAD、输出 DSTORE 依次为 `0x5A820E2B`、`0x5A80052B`、
`0x5A80092B`、`0x5A8002AB`。运行时仍由 `x10` 传 line offset、`x11` 传 line count；
4096 系数、一个 RNS 分量、`q=50061313`、四笔数据 span 和两阶段 PSYNC 屏障不变。
上游当前自测覆盖固定 STG/DMA 等向量，不再沿用试验分支的 16,384 组穷举描述。

## 仍须统一的定义

- MOD_ID出现编码256项、物理128项和应用64项三种表述；本次只使用已绑定的0/6。
- small bank物理32 lines与单次装载8 lines的说明并存；本次模表只装1 line。
- DSTORE表格的rel=0保留语义与正文“当前实现两种rel均释放”冲突。
  当前用例没有调用`dstore_keep()`；正向回环均使用rel=1，释放后不重复PFREE。

## IT运行条件

用例仍发合法的PSYNC `0x7000000b`。软件同步不能修复RTL的custom0/DASICS
重叠译码、DMA控制字或AWCACHE/ARCACHE属性。运行时应记录实际RTL版本及
CPU接收/HPU接收/PSYNC事件波形。构建成功不等于VCS仿真通过。
06、07、08 已收到 IT 未通过反馈。本次升级后应替换新 ELF/BIN 再跑并保留日志；
没有新的运行证据，不能把这些用例标成已通过，也不能推断所有失败都由旧编码造成。
