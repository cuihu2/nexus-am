# 08边缘快测与ST前置证据

既有算子N4096通过可作为回归基线，但不能据此宣布结构/故障场景已覆盖。
本轮新增八个独立定向用例，位于`src/08_cpu_hpu_structural_connectivity/03_edge_cases`。
机器清单为`edge-cases.tsv`，每项一个ELF，不把多个规模/profile串在同一文件运行。

| 用例 | 主要刺激与软件验收 |
|---|---|
| CMD_BURST_N001/N008/N009/N032/N064 | 真正连续1/8/9/32/64条HPU计算指令；PADD和PMUL立即数7交替，检查顺序/数量导致的算术结果及CPU s2/s3保存值 |
| CPU_HPU_ALU | 16条HPU算术命令与RV64字宽移位/XOR、CPU保存值交错；分别比较CPU和HPU结果 |
| CPU_HPU_MEMORY_BRANCH | RV运算、volatile load/store、条件分支选择HPU加/乘；比较CPU全部scratch、末值、HPU结果 |
| WATCHDOG_FAIL_STOP | 核侧命令入口反压超时，检查code=1、CPU继续运行、W1C不能恢复发令和DDR guard |

## 运行规模与同步

七项运算边缘用例仅操作1line、64个32bit系数；它们不替代N4096规模测试。
仍使用现有512line窗口和独立CPU影子，只有OUT的1line允许写入；
检查全部输出、输入、模表及所有未许可区域。
CPU scratch位于ELF的BSS，不在HPU window中。

看门狗用例单独准备4line（A/B各一line，另两line为guard），故意不提交窗口；
两份A/B仍来自inline-asm嵌入的N4096输入，本例仅取首64项，不做FHE运算。
它不调用正向初始化和完成等待，也不允许HPU写回任何区域。

全部HPU机器码取自固定inline-asm编码器，不在C里手拼位段。
连续burst使用汇编`.rept`展开，区间为`edge_burst_begin..edge_burst_end`，
期间没有C循环、函数调用、MMIO或printf。不同算子的交替比纯加法更容易暴露顺序错误。
混合区间用`edge_mix_begin..edge_mix_end`标记，CPU参考与实际执行分别实现。
运算边缘用例尾部DSTORE释放p0，随后PFREE p1/模表，再执行唯一PSYNC。
看门狗负向用例只在fail-stop后发唯一PSYNC，期望它被丢弃而不产生完成IRQ。

默认摘要与真正无UART的silent版本都保持同一自检。
`application_trace`在silent中仍保存阶段、状态、故障、IRQ和等待cycle；
若CPU卡在发令阶段，需要结合PC/队列信息定位。

## 软件等待不是硬件看门狗

`edge_wait(EDGE_WAIT_CYCLES)`默认1000000个cycle，只限制末尾MMIO等待，
不会使能timer或PLIC。它不是硬件看门狗的阈值或触发测试。
IRQ有效而BUSY尚未清零时继续等待；FAULT立即失败；缺IRQ/缺窗口有效状态不能假成功。

若CPU在HPU指令接受反压处已无法前进，软件还没有进入等待函数，
这个软件预算无法救出CPU；必须依赖IT总周期上限及实际RTL的硬件watchdog机制。
不把VCS周期超限、软件return1或旧DMA ack超时当成“新增看门狗已触发”的证据。

## 核侧看门狗的实际接口与负向用例

实现已定位到`cuihu2/IT-SCPU-RTL`的`two_core_no_fdi`分支：
`ff86d9ea50ed4fafb0a8f2cfecb958fa17404956`新增看门狗及故障码，
当前核对版本为`0cfd995302af926aa22767e145aade9fc9d21ca2`。
该分支的`FullSys.f`实际使用`RTL/latest/`；不能只看旧的`RTL/linknan/core/`副本。
不以main、`new`或NSN的`SCPU_rtl_dc`名字推断它们已合入同一机制。

- [HpuCmdMerge.sv](https://github.com/cuihu2/IT-SCPU-RTL/blob/0cfd995302af926aa22767e145aade9fc9d21ca2/rtl/cpu/SCPU_RTL/RTL/latest/HpuCmdMerge.sv)：
  custom0或custom1的输入valid保持、下游ready为0时计数，阈值为500000个CPU时钟。
  正常握手或没有输入valid时计数清零；不是从程序启动计时，也不是DMA ack超时。
- 超时后`cmd_timeout_fail_stop`保持，`io_cmd_valid=0`，共享CPU ready释放Fence，
  CPU可继续执行，但后续HPU命令全部被丢弃。这里只上报MMIO故障，不产生RISC-V trap。
- [TLDeviceBlockInner.sv](https://github.com/cuihu2/IT-SCPU-RTL/blob/0cfd995302af926aa22767e145aade9fc9d21ca2/rtl/cpu/SCPU_RTL/RTL/latest/TLDeviceBlockInner.sv)：
  `CSR_FAULT[0]=1`、`CSR_FAULT[15:8]=0x01`，该CPU故障的对象号/is_load为0，
  `CSR_STATUS[2]=1`。W1C只能清记录，仍置位的故障源会再次上报；它不能解除核侧fail-stop。
  恢复必须复位CPU和HPU，再单独启动下一例。

`WATCHDOG_FAIL_STOP`使用已核对的[controller阻塞条件](https://github.com/cuihu2/IT-SCPU-RTL/blob/0cfd995302af926aa22767e145aade9fc9d21ca2/rtl/cpu/SCPU_RTL/RTL/latest/hpu_controller.sv)：
窗口未提交时，DLOAD在`block_reason_local=8`停住，不发外存请求。
连续64条长度为1line、offset=0的DLOAD使下游队列积压，直到核侧入口持续反压并超时。
这是针对该交付版本的故障刺激，不是合法计算程序的初始化顺序；
其它设计若规定未提交请求立即拒绝/报错，不能照搬本例并反推同一语义。

程序核对精确故障类型，不把旧range fault、缺fault或纯软件超时当成功；
随后W1C、再发64条DLOAD和末尾PSYNC，检查CPU未重新等待一个硬件超时、故障仍可见、IRQ未置位，
并检查全部256个DDR word不变。普通版仅打印几条摘要，silent版不打印。
精确500000拍边界、计数复位、payload稳定、超时后`io_cmd_valid=0`和零额外AXI事务仍需IT monitor；
尤其guard不变不能证明没有额外DDR读取。仿真总预算必须给500000拍硬件等待、启动和自检留余量。

真实模块的独立RTL回归入口为：

```bash
python3 tests/hputest/scripts/test-watchdog-rtl.py \
  --rtl-repository /path/to/IT-SCPU-RTL \
  --ref 0cfd995302af926aa22767e145aade9fc9d21ca2
```

脚本直接读取该commit的活动RTL，不改原文件、不缩短硬件阈值，验证custom0/custom1超时、
最后一拍正常握手优先、输入撤销/握手清计数、sticky停发和复位恢复。
独立模块回归和ELF编译通过都不等于整机IT/VCS已经通过。

## IT monitor与ST准入

软件PASS不等于队列确曾满载。刺激1/8/9/32/64的作用是提供独立短入口；
实际填满8条缓存、第9条受控等待、释放后保序恢复，需要环境控制ready/队列排空并取证。
记录每条请求的接受/提交编号，检查反压期间payload稳定、无丢失/重复/串配。
burst的N仅计计算区间，不包含前导DLOAD/PMODLD及末尾DSTORE/PFREE/PSYNC。

分支用例还需检查错误路径HPU命令无副作用；CPU普通内存区、HPU窗口外AXI副作用
不能只依靠当前window guard排除。
完成这些定向项的实际IT验收并保留watchdog版本/触发证据后，
可以把这些指令链与固定seed作为ST扩展的起点；
不得把确定性C程序重新标为STING随机覆盖，也不把尚未实现的异常/特权入口计为通过。
