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

## 下载包

GitHub Actions只发布一个`nexus-am-hpu-tests`。普通minimal版本保留原文件名，真正无printf的
版本在扩展名前加`_silent`，例如`foo.elf`和`foo_silent.elf`。03只发布37个独立子项，
不发布原九个串行整例；其它章节使用原workload。14项未接入用例仍不发布占位二进制。

`INDEX.tsv`记录每个文件的`variant/log_mode/uart_results`。各模式仍先在独立构建目录完成
静态验证，再由组包器核对revision、producer、清单和文件集合后合并，防止混用。
全量逐系数结果不放进默认下载包，需要时仍可在本地使用`make diagnostic`构建。

## 本地构建

首次准备同批数据（已经make all时可跳过）：

```bash
export AM_HOME=/path/to/nexus-am
make -C tests/hputest verify-inline-asm
make -C tests/hputest all
python3 tests/hputest/scripts/package-chapters.py tests/hputest/build/artifact --all
make -C tests/hputest subtests
make -C tests/hputest silent
python3 tests/hputest/scripts/package-chapters.py tests/hputest/build/silent/artifact --silent
make -C tests/hputest silent-subtests
make -C tests/hputest unified
```

最终目录为`build/unified/release/hputest/`；其中保留并行运行脚本和四份输入构建清单。
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
