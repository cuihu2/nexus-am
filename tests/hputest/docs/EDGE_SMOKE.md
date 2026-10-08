# 08边缘快测与ST前置证据

既有算子N4096通过可作为回归基线，但不能据此宣布结构/故障场景已覆盖。
本轮新增七个可运行定向用例和一个明确待确认的看门狗记录，位于`src/08_cpu_hpu_structural_connectivity/03_edge_cases`。
机器清单为`edge-cases.tsv`，每项一个ELF，不把多个规模/profile串在同一文件运行。

| 用例 | 主要刺激与软件验收 |
|---|---|
| CMD_BURST_N001/N008/N009/N032/N064 | 真正连续1/8/9/32/64条HPU计算指令；PADD和PMUL立即数7交替，检查顺序/数量导致的算术结果及CPU s2/s3保存值 |
| CPU_HPU_ALU | 16条HPU算术命令与RV64字宽移位/XOR、CPU保存值交错；分别比较CPU和HPU结果 |
| CPU_HPU_MEMORY_BRANCH | RV运算、volatile load/store、条件分支选择HPU加/乘；比较CPU全部scratch、末值、HPU结果 |
| WATCHDOG_PENDING | 缺硬件设计/接口契约，不发非法刺激、不返回假PASS、不发布占位ELF |

## 运行规模与同步

全部新可运行项仅操作1line、64个32bit系数；它们不替代N4096规模测试。
仍使用现有512line窗口和独立CPU影子，只有OUT的1line允许写入；
检查全部输出、输入、模表及所有未许可区域。
CPU scratch位于ELF的BSS，不在HPU window中。

全部HPU机器码取自固定inline-asm编码器，不在C里手拼位段。
连续burst使用汇编`.rept`展开，区间为`edge_burst_begin..edge_burst_end`，
期间没有C循环、函数调用、MMIO或printf。不同算子的交替比纯加法更容易暴露顺序错误。
混合区间用`edge_mix_begin..edge_mix_end`标记，CPU参考与实际执行分别实现。
程序尾部DSTORE释放p0，随后PFREE p1/模表，再执行唯一PSYNC。

默认摘要与真正无UART的silent版本都保持同一自检。
`application_trace`在silent中仍保存阶段、状态、故障、IRQ和等待cycle；
若CPU卡在发令阶段，需要结合PC/队列信息定位。

## 软件等待不是硬件看门狗

`edge_wait(EDGE_WAIT_CYCLES)`默认1000000个cycle，只限制末尾MMIO等待，
不会使能timer或PLIC。它不是硬件看门狗的阈值或触发测试。
IRQ有效而BUSY尚未清零时继续等待；FAULT立即失败；缺IRQ/缺窗口有效状态不能假成功。

若CPU在HPU指令接受反压处已无法前进，软件还没有进入等待函数，
这个软件预算无法救出CPU；必须依赖IT总周期上限及待确认的硬件watchdog机制。
不把VCS周期超限、软件return1或旧DMA ack超时当成“新增看门狗已触发”的证据。

看门狗真实测试至少要明确：

1. 精确RTL版本/模块、受监视的事件（无进展、DMA ack、执行完成等）；
2. 使能、复位、计数起止与阈值单位，是否允许正常长期反压；
3. 可见故障/中断/状态和清除、复位、后续命令恢复机制；
4. 合法可重复的故障注入接口，及非法AXI副作用的monitor条件。

本地查到了历史DMA等待计数器，但没有依据把它等同于群里新增的watchdog。
接口确认前，这一项仅列入BLOCKED索引，不发布模拟通过程序。

## IT monitor与ST准入

软件PASS不等于队列确曾满载。刺激1/8/9/32/64的作用是提供独立短入口；
实际填满8条缓存、第9条受控等待、释放后保序恢复，需要环境控制ready/队列排空并取证。
记录每条请求的接受/提交编号，检查反压期间payload稳定、无丢失/重复/串配。
burst的N仅计计算区间，不包含前导DLOAD/PMODLD及末尾DSTORE/PFREE/PSYNC。

分支用例还需检查错误路径HPU命令无副作用；CPU普通内存区、HPU窗口外AXI副作用
不能只依靠当前window guard排除。
完成这些定向项的实际IT验收并保留watchdog契约/触发证据后，
可以把这些指令链与固定seed作为ST扩展的起点；
不得把确定性C程序重新标为STING随机覆盖，也不把尚未实现的异常/特权入口计为通过。
