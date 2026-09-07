# 2026-09-05 编程手册 0.4：用例同步更新

本次依据用户提供的 `HPU_PROGRAMMING_MANUAL.pdf`（22页，2026-09-05，v0.4）
第6.1、6.3、8.1节更新测试软件。2026-09-07 按用户确认，进一步采用该 PDF
第3.2节位段图及公式修正 STG 编码器和 AM 接收检查。随后按用户要求将该修正
从原 `HPU_SEAL` 分支撤销，改放独立试验分支 `HPU_SEAL_manual_0905`；AM 当前
固定引用提交 `62985e45702e9130a0aa39bca6140a3c4fd6c72a`，供 IT 试跑。
试验分支基于原分支回退提交 `5404959777c07c25b5b6a65e66020c74740f2e3f`
恢复修正，源码内容与原编码修正提交 `45b51d5` 相同。原 `HPU_SEAL` 保持回退
状态，本次未再次向原分支提交编码修改。
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

编码由试验分支的 inline-asm 生成，AM 不手工替换程序中的机器码。试验分支内 Markdown 手册
本次只同步 STG 格式和对应示例，不能据此认定其他章节已全部同步到该 PDF。
NTT/INTT 用例在 AM 中仍缺少完整程序、数据、golden 和 relocation 的端到端接入，
因此继续保持未就绪状态；原因不再是未选择 STG 公式。编码检查通过也不等于
NTT 功能已在 IT 环境通过。

## 仍须统一的定义

- MOD_ID出现编码256项、物理128项和应用64项三种表述；本次只使用已绑定的0/6。
- small bank物理32 lines与单次装载8 lines的说明并存；本次模表只装1 line。
- DSTORE表格的rel=0保留语义与正文“当前实现两种rel均释放”冲突。
  当前用例没有调用`dstore_keep()`；正向回环均使用rel=1，释放后不重复PFREE。

## IT运行条件

用例仍发合法的PSYNC `0x7000000b`。软件同步不能修复RTL的custom0/DASICS
重叠译码、DMA控制字或AWCACHE/ARCACHE属性。运行时应记录实际RTL版本及
CPU接收/HPU接收/PSYNC事件波形。构建成功不等于VCS仿真通过。
