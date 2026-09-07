# 编程手册 0.4：当前用例适配与历史差异

当前规范来源是 `inline-asm/main` 固定提交
`b405f2ad7b0901930d81edb79a5167ab028c4dbd` 中的
`doc/HPU_PROGRAMMING_MANUAL.md`。其版本标题仍为 v0.4，但 DMA 字段及
同步约定已经更新，不能把标题相同当作内容相同。

历史上 AM 曾按 2026-09-05 PDF 在模表 DLOAD 后增加屏障，并使用
`HPU_SEAL_manual_0905` 试验分支；这两项已被当前上游来源和约定替代。
本次只修改测试软件及接收检查，不修改任何 RTL 或历史 inline-asm 分支，
也没有进行 VCS 仿真。

## 明确落实的流程

新手册第 6.1、6.3 节明确：DMA、配置、计算和对象生命周期的依赖由硬件
维护，完整程序只在末尾发一次 PSYNC，模表 DLOAD 与 PMODLD 之间及算子
内部不插入 PSYNC。

- 06 保持纯 MMIO STATUS 同步：DLOAD → 观察 BUSY 1→0 → DSTORE →
  再次观察 BUSY 1→0 → 4096 系数自检；不发 PSYNC、不读取 IRQ。
- 07 连续提交 DLOAD、DSTORE 和末尾 PSYNC，再由 PLIC 中断完成同步。
- 08/09 直接编译已校验的上游 `mm.c`，调用 `hpu_program_mm()`，十条指令
  仅含末尾一次 PSYNC。08 通过 PLIC 中断等待，09 通过 MMIO 完成电平等待。
  不再派生 `mm_phases.c/h`，不再调用 `irq_rearm()`。
- PATH_001、STING_CMD_001、PATH_004、INS_C0_001/002/003/004/007、
  CMB_009、STR_002/006 删除模表 DLOAD 与 PMODLD 之间的屏障。
- INS_C0_003 的 DSTORE/对象复用和 INS_C0_009 的 DLOAD/PFREE/重新分配
  按程序顺序提交，不在中间插入 PSYNC；仍保留末尾完成和数据检查。

等待过程中仍检查 FAULT、窗口有效和忙状态，PSYNC 的 MMIO 等待必须同时
取得完成事件与 BUSY 清零的证据，不能重新引入旧快照误判。阶段诊断和最终逐系数
比较均保留，失败返回 1。用例数量和 ID 不变，上游 `mm.c/.inst32/.cmd26`
随工件保留，生成函数的 `hpu_obj_len` 检查不因移除阶段适配而删减。

## 已确定的 STG 编码

当前上游手册第 3.2 节的位段图、公式及编码器一致：

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
inst32 = (obj << 25) | (rs2 << 20) | (rs1 << 15)
       | (operation << 13) | (dir << 12) | (flag << 7) | 0x2B
DLOAD: dir=0, operation=type
DSTORE: dir=1, operation=rel << 1
custom0 cmd26 = inst32 >> 7
custom1 cmd26 = (1 << 25) | (inst32 >> 7)
```

此前把寄存器号放在 `[26:22]` / `[31:27]` 的 DMA 编码不再适用。
CPU 使用标准 `RS1=[19:15]`、`RS2=[24:20]`；其他字段仍必须按当前公式解析。
MM 的模表 DLOAD、输入 A DLOAD、输入 B DLOAD、输出 DSTORE 依次为
`0x06B540AB`、`0x02B5202B`、`0x04B5202B`、`0x00B5502B`。
运行时仍由 `x10` 传 line offset、`x11` 传 line count；4096 系数、一个 RNS
分量、`q=50061313` 和四笔数据 span 不变，整个 MM 程序只有末尾一次 PSYNC。
上游当前自测覆盖固定 STG/DMA 等向量，不再沿用试验分支的 16,384 组穷举描述。

## 当前长度与生命周期约束

- MOD_ID 编码为 8 bit，可表示 0..255；应用 ABI 限定 0..63，两者不是
  同一限制。当前测试使用的 0/6 不变，不据此启用更大模表场景。
- small bank 允许不超过 32 lines 的小对象，当前模表仍只装 1 line。
- DSTORE 实际传输长度为 `OBJ.len`，硬件忽略 `x11`；生成 C 仍设置非零
  `span.line_count` 并验证它等于软件跟踪的对象长度。
- DSTORE 的 `rel=0` 和 `rel=1` 都在成功后释放源对象，不能在成功写回后
  对同一份分配再 PFREE。当前正向回环仍使用 `rel=1`，没有调用 `dstore_keep()`。

## IT运行条件

要求完成通知的用例在程序末尾发 PSYNC `0x7000000b`，06 不发该指令。
软件同步不能修复 RTL 的 custom0/DASICS
重叠译码、DMA控制字或AWCACHE/ARCACHE属性。运行时应记录实际RTL版本及
CPU接收/HPU接收/PSYNC事件波形。构建成功不等于VCS仿真通过。
06、07、08 已收到 IT 未通过反馈。本次升级后应替换新 ELF/BIN 再跑并保留日志；
没有新的运行证据，不能把这些用例标成已通过，也不能推断所有失败都由旧编码造成。
