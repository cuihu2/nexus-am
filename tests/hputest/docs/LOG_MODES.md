# 少打印与完全静默版本

默认不再逐轮打印配置、阶段、DMA计划、成功系数或比较成功统计。
这次仅关闭日志开销，不减少用例轮数、数据量、指令序列、同步、golden或guard检查。
不能根据聊天中的一个下标判断10M周期超时一定由UART造成：旧brief也可能打印很靠后的错误项，
全量版则会依次打印全部4096项；UART不前进也可能卡在任意一次发送。

## 三种模式

| HPU_LOG_LEVEL | HPU_DUMP_RESULTS | 行为 |
| --- | --- | --- |
| 0（silent） | 0 | 不打印；跳过AM串口初始化和输出；只保留原返回码与仿真结束指令 |
| 1（minimal，默认） | 0 | HPU开始/最终PASS或FAIL；失败结果每块仅首错及总错误数；不打印成功系数/每轮阶段 |
| 2（verbose） | 0 | 详细配置/阶段/受限结果诊断，主要用于定位错误 |
| 2（verbose） | 1 | 完整BEGIN/DATA/END逐项输出，最慢，仅按需使用 |

最小版仍可能有少量公共AM启动日志；完全不需要UART时请选择silent。
minimal/silent不再采样阶段cycle或写`mcounteren.CY`，也不计算只用于详细误差报告的差值除法。
它们仍逐项读取并精确比较结果，不能把输出先取模或放宽误差来制造PASS。

关闭的日志不求值实参；原报告的block递增已移出日志参数，避免静默后漏更新软件状态。
关键CSR写读、自检、HPU发令、中断claim/clear及返回码均不放入可关闭的日志宏。

## 该下载哪个包

- `nexus-am-hpu-workloads`：原完整用例，默认minimal。
- `nexus-am-hpu-subtests`：03的37个独立子项，默认minimal。
- **`nexus-am-hpu-silent-subtests`**：37个独立子项，完全无printf，适合此次先并行复测。
- `nexus-am-hpu-silent-workloads`：原完整用例的无printf版本。

上述四包在普通push后生成；20项未接入用例仍不发布占位二进制。
`nexus-am-hpu-uart-results`不再默认发布。确需全量数据时，在GitHub Actions手动运行workflow，
勾选`full_uart_diagnostics`，才额外生成该12例慢速诊断包。不要继续使用旧链接里的全量包做速度复验。
每包的MANIFEST记录`log_level/log_mode/uart_results`，对象缓存也按模式分离，防止混用。

## 本地构建

首次准备同批数据（已经make all时可跳过）：

```bash
export AM_HOME=/path/to/nexus-am
make -C tests/hputest verify-inline-asm
make -C tests/hputest silent
make -C tests/hputest silent-subtests
```

静默完整包输出`build/silent/artifact`，章节下载目录由以下命令生成：

```bash
python3 tests/hputest/scripts/package-chapters.py tests/hputest/build/silent/artifact --silent
```

静默子项包直接输出`build/silent-subtests/release`，保留原并行运行脚本。
单例可使用`make one CASE=... HPU_LOG_LEVEL=0`；详细日志用`HPU_LOG_LEVEL=2`，
只有同时显式设置`HPU_DUMP_RESULTS=1`才输出所有系数。

## 如何判定结果

静默仅影响ELF中执行的printf/UART，不关闭仿真器自身的日志与PASS/FAIL报告。
有限用例依旧通过`main()`的0/非0及原`_halt`指令上报；超时、无日志不等于PASS。
无限等待看波形的01冒烟和故意return1的探针仍保持原特殊行为。
并行脚本的进程exit0仍不是用例PASS，需核对IT的终止结果。

静默构建不修改共享AM/klib或RTL：在链接阶段使用无输出空桩封住真实`printf_`、printf系列、
字符输出及UART初始化，并在每个最终ELF中静态检查真正的格式化器/输出驱动已排除、空桩无UART访问，
同时验证原`_halt`停止指令仍存在。验证涵盖原60例和37个subtest；不代表已在VCS执行通过。

如果静默版仍超时，应继续区分计算/等待/硬件故障，而不是无限增加cycle-limit或删自检。
需要定位时再用同版本的minimal或verbose单子项复现，并保留实际用例ID、log_mode、cycle-limit和simv版本。
