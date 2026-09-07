# 2026-09-05 编程手册 0.4：用例同步更新

本次依据用户提供的 `HPU_PROGRAMMING_MANUAL.pdf`（22页，2026-09-05，v0.4）
第6.1、6.3、8.1节更新测试软件。固定的inline-asm子模块版本未变；RTL未修改。

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

## 仍须统一的定义

- PDF第6页STG公式要求pdata同时写bits[27:25]和[24:22]，twiddle写[21:14]；
  其示例/附录却与当前encoder一致，把twiddle写[24:22]。
  `pntt p0,p3,15,0,0`分别得到`0x4000fc0b`和`0x40c03c0b`。
  未擅自选择一套，NTT/INTT测试继续保持未就绪状态。
- MOD_ID出现编码256项、物理128项和应用64项三种表述；本次只使用已绑定的0/6。
- small bank物理32 lines与单次装载8 lines的说明并存；本次模表只装1 line。
- DSTORE表格的rel=0保留语义与正文“当前实现两种rel均释放”冲突。
  当前用例没有调用`dstore_keep()`；正向回环均使用rel=1，释放后不重复PFREE。

## IT运行条件

用例仍发合法的PSYNC `0x7000000b`。软件同步不能修复RTL的custom0/DASICS
重叠译码、DMA控制字或AWCACHE/ARCACHE属性。运行时应记录实际RTL版本及
CPU接收/HPU接收/PSYNC事件波形。构建成功不等于VCS仿真通过。
