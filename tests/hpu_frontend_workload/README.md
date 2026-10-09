# 新API实验workload

本目录单独验证inline-asm重构后的ApplicationBuilder/Evaluator和资源管理。**不替换旧hputest，不更改00～08组或已通过基线**。

依赖固定为`feature/operator-resource-management@9ebd89f`，不是旧workload的main/legacy-main。完整commit见[producer.json](producer.json)。API和应用图直接采用该分支的`examples/*_evaluator_application.cpp`，不是用AM重新实现一套算法。

## 用例

| 独立ELF | 应用 |
| --- | --- |
| CKKS_N128、CKKS_N4096 | `x × (RotateLeft(x,1) + Conjugate(x)) + 1`，显式reline/rescale |
| BFV_N128、BFV_N4096 | `x × (RotateRows(x,1) + RotateColumns(x)) + 7`，显式reline/modswitch |
| BGV_N128、BGV_N4096 | 同上，保留BGV的correction factor和模上下文 |

每个ELF只跑一个scheme、一个N、一张7节点应用图，不串行跑多个规模或重复轮次。图中的算子依赖仍必须按顺序执行。6个ELF可在用例外部分别并行仿真；不能把同一图的依赖节点任意分拆。

可读的main放在`src/`，CSR操作直接可见；共享数据搬运、同步、自检分别放在`runtime/image.c`、`wait.c`、`check.c`。大型producer生成C和数据仅位于忽略的build目录和下载包，不提交进Git。

## 构建与下载

```sh
git submodule update --init tests/hpu_frontend_workload/third_party/inline-asm
make -C tests/hpu_frontend_workload all
```

产物是`build/hpu-frontend-workload.zip`；GitHubActions另有独立的`build experimental HPU frontend workload`工作流，下载`nexus-am-hpu-frontend-workload`，不会混入旧`nexus-am-hpu-tests`包。

压缩包的`minimal/`、`silent/`各包含6套ELF/BIN/TXT。正常包只打印阶段、CSR错误、前4个结果错误及错误总数，不全量打印4096项。静默版关闭用例打印、AM启动UART初始化和字符出口，自检与return码不变；可读取`workload_trace`定位阶段和首错。

`delivery/<case>/upstream/`保留输入、密钥、twiddle、全部节点golden、语义解密报告、应用图、DMA清单和资源报告；`target/`保留实际嵌入镜像、输出毒化、只读检查mask及末端guard。数据、指令、x10/x11和DMA spans均来自同一次producer交付，**不手写编码，不套用旧BFV布局/CKKS取整适配**。

AM将镜像搬到`0x87000000`，按真实window行数配置MMIO，再执行producer程序。末尾仅一次PSYNC；CPU轮询`IRQ=1 && VALID=1 && BUSY=0`，清完成电平，然后逐节点、逐RNS原始word比较golden，同时检查只读数据及窗外guard。无需PLIC或定时器中断。默认完成等待预算2500万模拟cycle，可通过`WAIT_CYCLES=...`调整；IT模拟器自身的cycle limit也必须足够。发令阶段若受RTL反压阻塞，不能靠完成轮询的预算解除阻塞。

仅构建一种UART模式可用`make minimal`或`make silent`。大构建请把`OUTPUT_ROOT`设到磁盘目录，不放`/tmp`。改变接收器或producer版本时使用新的输出目录，已有交付不会被接收器覆盖。

## 验收边界

构建门禁包括SEAL/软件模型raw golden、优化等价、严格host instruction model、native包验证、DMA子区间/只读限制、全部输出覆盖、ELF真实指令字比对、加载段与DDR不重叠，以及静默ELF无UART路径检查。任何门禁失败都会停止构建。

**下载包状态是`BUILD_READY_NOT_IT_PASS`，不是硬件通过。**该分支的host默认采用`natural_coefficient_boundaries`：在首PNTT之前和末PINTT之后做软件模型的bit-reversal边界转换，而不额外发出重排指令；其`hardware_contract_verified=false`。本workload原样保留新的`ntt_table_abi`，不偷偷改数据来适配旧RTL。必须用支持此合同的实际RTL跑VCS并比对，才能宣布IT通过。

CKKS的解密误差/容差记录在producer的`semantic/decoded.json`；目标端按同一交付的原始RNS word严格比较，不能用近似容差掩盖指令、布局或取整错误。这里没有在裸机上解密，也没有把host通过冒充VCS结果。N128是诊断参数；参数安全级别以交付metadata为准，不用于声称密码学安全性。
