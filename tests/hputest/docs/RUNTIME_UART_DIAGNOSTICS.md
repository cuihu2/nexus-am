# 03/04 耗时与 UART 正确性诊断

## 已确认的问题与尚不能确定的原因

上一版不是只加注释：03 的 PMAC 由1轮增加为4轮，PNTT/PINTT由未发令占位变为各6轮。
每轮还初始化512line（128KiB），并完整比较非输出位置；04整体NTT/INTT和BConv此前也没有真实执行。
因此旧版本的运行时间不是同工作量基线。仿真运行几小时是墙钟时间，不能直接等同用例消耗多少cycle。

对上一版实际ELF的静态分析发现，PMAC每轮仅shadow初始化、DDR复制、guard检查三段约77万条指令，
尚不含golden、缓存维护、UART与HPU等待。4轮自然不能假定100万cycle足够。
这是静态成功路径估算，不是VCS实测，也不是新的cycle预算。

本轮修复不改变数据与检查范围：

- 两张边界数组移出4096次循环，消除每系数重复的13条栈写入。
- producer输入已小于q时不再执行动态取模；较小q仍走原来的取模语义。
- guard权限改为逐line查询，仍检查每一个非输出word。
- guard只失效其实际读取的只读段，结果比较负责失效输出，去掉重复CBO。

默认全部round和末尾PSYNC规则未改变；没有缩小guard、增大允许误差、降低系数数量或改RTL。
没有本次完整超时日志/波形，尚不能断言所有超时都由工作量造成，也不能认定是硬件死锁。

## 看阶段日志，区分 CPU 开销和 HPU 等待

03/04打印 `[HPU][PHASE]`，包括当前phase、cycle、上一phase及CPU经过的cycle：

```text
current=prepare
current=software-golden
current=configure
current=issue
current=wait-completion
current=compare-results
current=check-guard
current=round-done
```

各例按实际步骤出现相应阶段；完整算子的golden已在构建阶段验证，不在CPU重新做全变换。
`cpu_elapsed`衡量上一条phase日志结束到当前采样之间的CPU时间，排除了phase日志自身打印，
但仍包含阶段内其它UART输出和软件开销，**不是HPU纯计算周期**。
初始化只在M-mode的main入口置`mcounteren.CY`，让后续S-mode能读同一个`cycle`；不会开启计时器中断。

- 最后停在prepare/golden/guard：可能尚在CPU数据准备或自检，应结合cycle推进判断。
- 最后停在wait-completion：保留IRQ/STATUS/FAULT日志并查PSYNC、DMA、PLIC波形。
- 出现完整RESULT END FAIL：确有数据/规范性不一致，不是单纯“还没算完”。
- 达到仿真MAX_CYCLES而没有END：结果块不完整，不能当作结果误差或PASS。

`TIMEOUT=20000000`的单位是轮询次数，不是仿真cycle；一次轮询会执行多条指令和多个MMIO。
不要拿它与100万cycle直接比较。我们不自动改仿真器cycle-limit，也不改FGP/多核选项。

## 单独定位一轮，默认仍完整覆盖

无需自己构建时，可下载新增的`nexus-am-hpu-subtests`：03的37个独立选择项已经分别固化到ELF/BIN，
并提供用例外并行工具。详细文件映射和运行方式见 [subtest目录](https://github.com/cuihu2/nexus-am/tree/master/tests/hputest/subtests)。

AM已有mainargs入口。03可通过`subcase=N`只运行一组，源码不需要删循环；空参数或`all`运行全部。
选择子集会打印 `coverage=selected-subset-not-full`，不能计作整例全覆盖。

| 03用例后缀 | N范围 | 含义 |
| --- | --- | --- |
| 001/002/004 | 0..3 | 原variant编号 |
| 003 | 0..5 | 原variant编号 |
| 005/006 | 0..5 | `profile*3+selected`；selected对应stage0/1/11 |
| 007/009 | 0..1 | 原profile编号 |
| 008 | 0..2 | idle/DMA/compute，每个选中场景仍完整执行两轮irq/rearm |

例如只构建PMAC第0轮（诊断默认仍摘要）：

```bash
export AM_HOME=/path/to/nexus-am
make -C tests/hputest one \
  CASE=03_compute_instructions/01_basic_compute_and_transform/HPU_IT_DIR_INS_C0_004 \
  mainargs=subcase=0
```

增加`HPU_DUMP_RESULTS=1`可单独导出该轮全部结果。`mainargs`记入产物清单；全量CI包使用all。
04每个完整算子目前只一段程序，支持all或subcase=0，不能从中间NTT stage开始执行。
多用例并行和VCS FGP不是一回事；本次没有更改仿真调度器或开关，也不保证相同墙钟加速比。

## 两种下载包

- `nexus-am-hpu-workloads`：常规包，摘要、前4项和最多8个错误，完整检查所有结果。
- `nexus-am-hpu-uart-results`：仅03九项和04 BConv/NTT/INTT三项；每个结果逐项打印actual/golden/q/delta。

全量包不是加速包。UART是阻塞输出，4096项、多RNS或多round可能产生大量日志，仿真会更慢。
先用摘要确定哪一例/哪一轮有问题，再用全量或单子项构建定位。
两包使用同一固定producer数据和编码；对象目录隔离，避免用brief旧对象误构建full包。

## 整数误差判读与 CSV 导出

03和当前04检查的是精确的模q整数，不是解密后的CKKS浮点精度。
输出必须等于golden且处于`[0,q)`。`q_multiple_mismatches`表示差值为非零q倍数，
有助于发现“同余但未规范化”，但仍失败；不能先对actual取模再把测试改成PASS。

UART协议`[HPU][RESULT] BEGIN/DATA/END`明确case、round、block、phase、地址、长度、q。
DATA含每项实际值、独立golden及有符号差值；END含错误总数、首错、最大绝对差、q倍差和非规范结果数。
即使数据失败也打印整个结果块并检查guard；PMODLD/BConv继续打印其余分量后统一失败。
未完成或FAULT的输出不冒称有效计算结果；因此在同步失败时不会打印带PASS语义的结果块。

```bash
python3 tests/hputest/scripts/parse-uart-results.py sim.log --output results.csv
# 下载包内也附同一工具：
python3 tools/parse-uart-results.py sim.log --output results.csv
```

工具只接受完整full协议，拒绝brief、缺END、重复/乱序index及统计矛盾；失败不生成部分CSV。
这不是解密器。若后续要比较真实密文解密误差，还需要对应算法库、密钥、scale/level、
预期明文与精度规范，不能凭这些原始RNS整数替代FHE解密。

复验请回传完整文件名、AM/RTL/simv版本、mainargs、cycle-limit、最后PHASE和RESULT日志。
