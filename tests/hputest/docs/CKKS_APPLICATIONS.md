# CKKS Reline 与应用增量接入

来源：inline-asm `main`，固定提交 `b5398a3cbe6dbd3a5a0d9abbef06425b0107f300`，
由既有 `third_party/hpu-seal` 接入。`third_party/inline-asm` 的 legacy-main 固定版本不变。
不更新 BFV 用例，不替换 APP001，不改变原有章节、workflow 和 03 subtest 结构。

| 新用例 | 上游 example | 固定参数 | HPU 指令 / DMA |
|---|---|---|---|
| CMB012 | 本地薄生成器，调用 `CkksOperationPlan::append_relinearize` | N=4096，Q4\|P1，3→2 components | 2753 / 1055 |
| APP002 | ckks_polynomial_x2_plus_one.cpp | N=65536，Q4\|P1→Q3，scale=2^30 | 4388 / 1652 |
| APP003 | ckks_composed_application.cpp | N=128，Q3\|P1→Q2，scale=2^20 | 4411 / 1774 |

APP002：`x²+1`，输入槽 `[0.25,-1.5,2]`，期望槽 `[1.0625,3.25,5]`。
APP003：`x*(RotateLeft(x,1)+Conjugate(x))+1`，输入槽 `[0.25,-1.5,2,0.75]`，
期望槽 `[0.6875,0.25,6.5,1.5625]`。这两个例子不等同于原 APP001 的 A*B+C/向量规约。
CMB012直接交付三分量乘积密文，程序中不包含Multiply或Rescale；输入区因
NTT→coefficient转换可写，密钥、KeySwitch常量、twiddle和64-line尾部guard不可写。

## 验收边界

每个 ELF 只执行一个完整程序一次。程序内部的 stage、RNS 分量、KeySwitch digit
是应用依赖链，不能作为互相独立的 testcase 拆开。仅在最后 DSTORE 后发一次 PSYNC，
通过 MMIO 检查完成电平及空闲，再清电平并进行比较；不依赖 PLIC。

主机端原样运行上游例子：独立 SEAL Evaluator 与 HPU 软件执行器比较最终密文，
并解密检查浮点误差（APP002 ≤5e-3，APP003 <1e-2）。成功后才导出数据。
目标 ELF 不链接 SEAL/私钥，不在目标端解密，而是逐字比较最后的 NTT/RNS 密文：
APP002 393216 个 uint32，APP003 512 个 uint32。这里必须精确相同，
不能用 CKKS 近似误差容限掩盖硬件整数计算错误。

不把上游 `expected_outputs.csv` 里的所有 scratch 当作独立正确 golden：
低层暂存区可能不由高层软件 executor 填写。只验收明确的最终密文输出。
根据真实 DSTORE manifest 生成可写掩码，其他区域（输入/密钥/twiddle/空隙/64-line
尾部 guard）不得变化；最终输出先填 `0xffffffff`，防止未执行却误通过。
程序 C、ASM、INST32、CMD26、resolved DMA 均核对后原样链接，不改操作数字段。

## 运行规模与日志

APP002 原始镜像 250883 lines（约61.25 MiB），加64-line guard后配置250947 lines，
DDR 地址 `0x87000000` 起。IT 必须提供覆盖整个镜像、golden、栈及此窗口的有效 DDR；
构建检查 ELF 末尾不与窗口相撞，但不能代替 IT 内存模型容量确认。
APP003 镜像499 lines，加 guard配置563 lines。
CMB012 镜像11458 lines，加 guard配置11522 lines。
N=65536 不应按 N=4096 冒烟的 cycle-limit 或耗时估算；本次未偷偷降规模。

两例跟随既有统一下载包进入 `07_full_application/01_application_demo/`。
普通版保持 minimal，另有 `_silent`，不默认逐系数打印。
详细阶段诊断可单例构建 `HPU_LOG_LEVEL=2 HPU_DUMP_RESULTS=0`；
仅确实需要完整数据时设 `HPU_DUMP_RESULTS=1`，会显著拖慢仿真。
同一次构建的普通/静默版复用同一份随机密钥和输入；不同生成批次不能混用 golden。
provenance/ckks-data 下保留每例的版本、布局、重定位、指令、镜像、最终 golden
及 `HOST_ORACLE.log`。后者是主机 oracle 结果，不是 IT/VCS 通过证据。

当前资格是 BUILD_READY_NOT_IT_PASS，必须在实际 RTL 上验证，不能把编译或主机检查当作硬件通过。
