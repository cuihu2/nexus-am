# CMB004长时间运行与CMB012失配

没有那次运行的revision、PC、日志与波形，不能确定唯一运行时原因。
这里分别记录源码成本、已复现的交付错误和仍需目标证据的部分。

旧CMB004只调用一次`hpu_program_keyswitch()`，subcases=1。
digit、Q/P limb和NTT stage是同一算子的内部步骤，并非重复多组独立测试。
固定N4096/Q4/P3/D2程序有2167条指令、716条DMA。

| CPU工作 | 数量 |
|---|---:|
| 14721-line初始窗口复制 | 942144个uint32，3768576字节 |
| cache clean | 58884个64 B block |
| 最终结果比较 | 32768个uint32 |
| 输入/密钥/常量/guard比较 | 794688个uint32 |
| 结果和只读区cache invalidate | 51716个64 B block |

这些均计入模拟cycle，关闭UART/波形也不能省去。墙钟十小时既可能对应大量
模拟cycle，也可能卡住，没有PC/cycle不能在两者间归因。

旧`TIMEOUT=20000000`是MMIO轮询次数，而且只在issue结束后才启动。
每轮多个MMIO、循环和总线延迟使它远超过二千万cycle。
CPU若阻塞在custom指令，根本进不了软件超时循环。

新版增加独立N128定位项、cycle预算和静默内存trace，并减少复制循环与重复cache
失效；N4096完整链及全部检查保留。不是把缩小规模视为原测试通过。

CMB012所用CKKS指令展开未采用rounded-P ModDown，而其高层软件/SEAL golden
采用了取整。逐指令复放可以复现八个limb失配；补齐纯模运算取整后精确通过。
单步不取整的解密差额很小，但raw word仍全部不同。选择不同golden语义需要明确
独立reference与精度条件，不能仅把失败word作为新的expected。

BFV另有系数排列差异，CKKS组合流程另有前序NTT输出被覆盖的问题。
详见[交付与精度说明](SCHEME_DELIVERY_V1.md)。这些可复现软件问题不能证明
无日志运行的唯一原因；硬件仍失败时按trace/PC检查命令反压、对象释放、DMA ack、
fault及最后PSYNC。重跑必须使用同一包的新ELF/BIN与匹配RTL。
