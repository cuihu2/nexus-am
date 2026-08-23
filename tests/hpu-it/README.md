# HPU IT 测试用例

本仓库按 `HPU-IT测试点分解.xlsx` 实现 HPU/SCPU 集成测试。当前版本包含
46 个测试点对应的 49 个 testcase；每个 `.c` 文件都有独立描述符、可复现
seed、可执行行为或明确的阻塞声明，以及外部证据需求。当前 48 个核心用例可发出
真实 HPU 程序并自检；仅 CMB_015 因外部算法契约缺失而显式阻塞，不会被错误标成
qualification PASS。另有 4 个生成算子补充用例。

测试计划基线：

- 7 个测试分类、15 个功能目录
- 46 个唯一测试点、49 个唯一 testcase
- testcase 优先级：P0 20 个、P1 8 个、P2 11 个、P3 10 个
- 计划文件 SHA-256：
  `a483d9b10c8626ced01d9e3cad55ce044e1884980b6c1cc9390d35737129f83d`

## 结果含义

宿主机运行使用软件对象模型检查数据准备、模运算 oracle、对象生命周期、
边界 guard 和 testcase 分派。RISC-V 版本则通过固定 `.word` 编码真正发出
HPU custom0/custom1 指令，并访问 IT 目标 CSR。

程序输出只区分以下两种结果：

- `PASS`：程序执行成功，输出与 golden 一致，且 guard、FAULT、timeout 等
  自包含检查全部通过。
- `FAIL`：程序检查、超时、FAULT、golden 比对或 guard 检查失败。

描述符中的 `requirements` 继续作为 monitor、ready/backpressure、cache、覆盖率
和性能数据的采集提示，但不再参与 guest 程序的 PASS/FAIL 判定。需要保存这些
辅助证据时，bind/UVM/RTL monitor 可以向与 guest UART/sim log 隔离的 evidence
文件先写一条
`HPU_IT_EVIDENCE_V1 case=<case> producer=<bind|uvm|rtl-monitor...> run_id=<id>`，
再写多条可追溯的
`HPU_IT_REQ_PASS case=<case> requirements=0x<mask> source=<bind|uvm|rtl-monitor...>`。
这些记录不改变 guest 已经输出的 `PASS` 或 `FAIL`。

无需 VCS/Spike 即可单测这个证据协议：

```bash
bash scripts/test_requirement_evidence.sh
scripts/check_requirement_evidence.sh \
  HPU_IT_DIR_STR_002 0xa13 /path/to/monitor.evidence
```

`check_requirement_evidence.sh` 只读外部 evidence 文件并校验记录：完整覆盖返回 0，缺位返回
77，畸形、错 case 或越权位返回 78。它不会生成 monitor 证据，也不参与
guest 的 PASS/FAIL 判定。

`HPU_REQ_READY_CONTROL` 只表示 ingress ready pattern；
`HPU_REQ_QUEUE_CONTROL` 额外要求队列 count、消费暂停/恢复、full 边界及
同拍出入证据。`HPU_REQ_CAPACITY_CONTRACT` 要求从目标 RTL/profile
导出并保存对象 bank/half/line 几何与映射；不允许仅根据逻辑对象号
推测容量。

IT RTL的custom1路径采用不增端口的borrowed-ready协议：Fence在既有
`hpu0Cmd.ready`确认前保持custom1 valid、指令、offset和length，Merge在命令
真正进入异步FIFO时才确认。交付级Fence+Merge测试已覆盖四周期反压和最早合法
背靠背命令；内部8深度FIFO也已支持full状态同拍pop/push。不过CPU前面仍有
4深度AsyncQueue和decoder缓冲，所以PATH/STR用例保留READY/QUEUE monitor位，
不能用“软件发了第9条”替代内部队列波形。

## 目录结构

```text
<测试分类>/
  <功能>/
    HPU_IT_<类型>_<编号>.c
runtime/
  hpu_test.h
  hpu_test.c
  hpu_test_main.c
  hpu_vectors.h
  hpu_vectors.c
scripts/
  prepare_inline_vectors.sh
  build_nexus_am.sh
  validate_artifacts.sh
```

一级目录对应测试计划 `Spec`，二级目录对应 `feature`。每个 testcase 文件只
定义 `HPU_DEFINE_TESTCASE(...)`；公共执行逻辑集中在 `runtime/`，防止 49 份
CSR、DMA 和 golden 代码漂移。

## HPU 接口约定

- HPU CSR MMIO 基址：`0x08000000`
- CSR 偏移：BASE_LO `0x00`、BASE_HI `0x04`、SIZE_LO `0x08`、
  SIZE_HI `0x0c`、COMMIT `0x10`、STATUS `0x14`、FAULT/W1C `0x18`、
  IT IRQ level/clear `0x1c`
- HPU line：256 字节
- testcase 默认 HPU window：`0x87000000`，19201 lines（4915456 bytes）
- Nexus-AM 程序入口和唯一 ELF `PT_LOAD`：`0x80000000`

`0x87000000` 被选在 IT runtime 的 128 MiB 映射边界
`[0x80000000, 0x88000000)` 内，19201-line window 的尾地址为
`0x874b0100`，仍留在该映射内。CPU 先在该地址准备输入，
执行 Zicbom cache clean；HPU DMA 访问同一物理内存；完成后 CPU invalidate
再比对结果。这避免默认 `0x10000000` 同时承担启动 flash、且 HPU master
没有对应 backing store 的地址冲突。

IRQ 清除按目标 RTL 的电平实现执行“写 1、再写 0”。程序不使用 STATUS bit1
判断计算完成，而是在末尾发出 PSYNC 并等待 IRQ/FAULT；完成后才断言 busy 已
清零。CFG-002 先写 window B 的 shadow、不提交并以相同 offset 验证仍走
window A，再 COMMIT 后验证切到 B。每次结果比对同时核对整个 HPU window 的
历史快照，能够发现输入、旧输出或候选 B 目的行被意外改写。

软件采样无法保证捕获短暂的 STATUS busy 脉冲，也不能替代 CSR 总线侧对
shadow/active 同拍切换的观察，因此 CFG-002/CFG-004 仍保留
`HPU_REQ_IT_MONITOR` 作为附加观测提示；guest 仍只根据执行和输出检查报告
`PASS` 或 `FAIL`。

当前目标 Chisel 的 `TLDeviceBlock.scala` 已把 BASE_LO/BASE_HI/SIZE_LO/SIZE_HI
实现为零初值 `RegInit`，并由轻量 generator 机器生成、同步到两份当前 IT 交付
RTL。`IT/unit_tests/B5_hpu_csr_reset` 的真实 TL regmap 动态测试覆盖复位为零、
四次 32-bit 写、两次 64-bit 回读、再次复位归零。CFG-001/CFG-004 仍保留正式
IT monitor 要求：本地动态测试证明修复闭环，但不能替代集成环境中的 CSR 总线
波形和 reset 时序证据。

CFG-001/CFG-004/CFG-005 的 RISC-V 生产路径会绕过普通 `rt_dload/rt_dstore`
范围预检，直接发出受控 raw custom1：覆盖窗口越界 DLOAD、零 `line_count`
DLOAD，以及 DSTORE 的零 `line_count` 和窗口越界；随后
有界轮询 `FAULT_STATUS[0]`，核对方向和 p0/p7 对象号，执行 bit0 W1C，并确认
整个 DDR window 无副作用。宿主机和 MMIO-disabled standalone Spike 使用确定性
CSR 模型，不执行无法由该环境表达前置条件的 raw custom1，并在日志中明确标记
`csr_mmio=UNMODELED`、`raw_custom1=NOT_ISSUED`。CFG-005 的 RISC-V/MMIO 生产路径
依次覆盖：未 COMMIT 时 p0 DLOAD fault；先建立一行 p7 对象、再 COMMIT `SIZE=0`
后的 DSTORE fault；以及恢复有效 window 后零 `line_count` DSTORE 和非零
`line_count` 的越界 DSTORE。各阶段均核对方向、对象号、W1C 和完整 19201-line
window 不变，不插入中间 PSYNC。当前 RTL 已实现 window-invalid fault 并消费该
uop；descriptor 仍保留 `HPU_REQ_FAULT_INJECTION | HPU_REQ_IT_MONITOR |
HPU_REQ_CACHE_CONTRACT`，独立 IT monitor、FAULT 注入和 AXI/cache 零副作用证据仍
标为 `EXTERNAL_REQUIRED`，本地功能闭环不能替代正式 IT 资格认定。

CFG-003 以独立、每阶段重置的 fixture 覆盖 p0/p7、1/2-line，以及 window
起点、中点和末端（19201-line profile 的 offset 19200）；DLOAD 边界和 DSTORE
边界分别执行，避免末端源/目标重叠掩盖 guard 损坏。PATH-003 覆盖 p0/p7 的
1/2-line DLOAD→DSTORE 回环；PATH-004 分别写回原对象和 PADD 结果，并覆盖
单次及连续 DSTORE。每个 phase 依靠对象依赖保持命令顺序，只在末尾执行一次
PSYNC，避免把完成 IRQ 错当成中途 barrier。每个目标区都先 poison，所有
阶段都逐字检查输出、相邻 guard 和完整 19201-line window。DLOAD 与 DSTORE
均通过 custom1 `rs2` 提供非零 line count；这些探针在每次 DSTORE 前装载实际
写回行数。AXI 首地址、burst/行数、写事务
次数和 cache 可见性仍只能由真实 IT monitor 关闭，因此三者继续保留
`HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT`。

CMB-002/CMB-003 和 PERF-002/PERF-003 使用真实的 `N=4096`、Q0..Q3
四模数基数据。构建时从 inline-asm 的 ciphertext-multiply reference 包切出
2305-line 紧凑镜像：四 limb 输入、独立输出、模表以及每个基的 pre/post factor
和 12-stage twiddle。输出区在执行前以 poison 覆盖，golden 只保留在 ELF
rodata；运行后逐字比较 16384 个结果，并检查其余 19201-line window 未被改写，
所以未执行 DSTORE 不可能利用预装 golden 假通过。

冻结 ABI 没有独立 bit-reversal 指令；真实 HPU 应由 `stream_ctrl` stage 地址
生成和 PE lane transpose 形成 radix-2 DIT 的等价配对。standalone HPU Spike
以 canonical vector 建模，因此在 stage 0 内执行等价 bit-reversal；INS-C0-005/
006 的 isolated stage-0 fixture 相应保留自然序输入，stage 6/11 fixture 才包含
bit-reversal 和 prior-stage 状态。Q4 CMB-002/003 与这些 isolated stage 用例曾在
历史 4096-line standalone 兼容 profile 上逐字通过，但该结果不证明真实 RTL 的 low/high-stage
物理 line/lane 配对正确；正式关闭仍需真实 RTL 的逐 stage、最终 golden 和
monitor 证据。

INS-C0-004 直接 DLOAD 独立累加初值到 p2，依次覆盖 PMAC 对象模式以及
`cimm8=0/1/255`，四个输出区均先 poison，再按“初值 + 乘积”逐系数比较并核对
完整 window。INS-C0-009 先以 `DSTORE rel=0` 保存 p0/p7 基线，随后交叉保留
另一个对象并分别 PFREE、装入完全不同的新数据，最后把两个复用对象写到第二
区域；两个用例都只在程序末尾发出一次 PSYNC。命令握手和 PFREE 期间无 DDR
事务仍需 IT monitor 证据，因此描述符保留对应外部需求位。

CMB-001 使用 `N=4096`、`q0=50061313`、`q1=50077697`、
`p0=90062849` 实现非退化的 Q2→P1→Q2 双向 BConv。对每个系数，
`x_j = a_j * (Q/q_j)^-1 mod q_j`，
`b = x_0*(q1 mod p0) + x_1*(q0 mod p0) mod p0`；反向单源基的
`Qhat=1`，因此得到 `b mod q0` 和 `b mod q1`。DDR fixture 位于 line
64..831，共 768 行（196608 bytes）；每个 limb 为 64 行。执行时只保持
p0/p1/p2 三个 64-line 活跃对象，分别小于 regular bank 的 1024-line 容量，
p4 small bank 仅保存一行模表。五个 scratch/输出区预先 poison，最后逐字检查输入、常量、
scratch、输出及完整 19201-line window。固定指令序列为一次模表
DLOAD，两段 `PMODLD/DLOAD/DLOAD/PMUL/DSTORE rel=1`，目标基的
`PMUL+PMAC/DSTORE rel=1`，再以两段 `PMUL/DSTORE rel=1` 转回 Q2；
p0/p1 在每次运算后 PFREE，p2 由 release-store 回收，全程仅有一条末尾
`PSYNC` (`0x7000000b`)。同一 CPU oracle 驱动 host 和 target 比较，该
fixture 已在历史 4096-line standalone HPU Spike 兼容 profile 逐字通过；
AXI 事务、cache 可见性和 ready/backpressure 仍保留 IT monitor/cache 外部证据位。

## HPU 指令来源

[`cuihu2/inline-asm`](https://github.com/cuihu2/inline-asm) 以 submodule 固定在：

```text
third_party/inline-asm @ 4399883b9e1fa249b99d48c7e919ee52acc662bc
```

普通 GNU assembler 不识别 HPU 助记符，因此 `hpu_inline_asm.h` 使用与上游
encoder/self-test 一致的固定 32-bit 指令字，并为 custom1 把 line offset 和
DLOAD/DSTORE 的非零 line count 绑定到 `x10/x11`。适配层覆盖
NTT/INTT stage 0..11、MOD_ID 0..6、PMAC
对象/`cimm8` 模式以及 p0/p2/p7 的 `DSTORE rel=0` 生命周期写回。
上游同时提供算子代码生成、line map、reference 和 golden 数据；
`scripts/prepare_inline_vectors.sh` 会先运行并验收 `hpu_delivery`，再允许 host
或 Nexus-AM 编译。CMB-001 已接入完整 Q2→P1→Q2 BConv 常量、scratch、
逐 DMA 重定位和同输入 golden。CMB-004/PERF-005 已接入真实
`N=4096, Q4/P3, D2` KeySwitch，CMB-012/PERF-001 已接入 RelinOnly，
PERF-006 和 CMB-010 已接入完整 ciphertext-multiply/HMUL 数据流。
这些路径共享inline-asm master镜像、31个broadcast常量，并使用与
secret-key区分离的192-line scratch和64-line尾部guard；CPU oracle、中间
checkpoint、最终golden和完整window保持检查已形成软件/host闭环。
真实RTL上的命令/AXI/cache证据与性能门限仍是外部验收项，
不得由host或standalone Spike结果代替。

CMB-010的xlsx行仍写有“阶段依赖间插入PSYNC”，但
`HPU_PROGRAMMING_MANUAL.md`/LATEST_SPEC已覆盖该旧描述：冻结实现依靠
对象数据依赖排序，整条ciphertext-multiply/HMUL程序仅在末尾发出一次
PSYNC。这是已记录的规格豁免，xlsx需待修订；不应把中途同步写回测试实现。

性能用例输出同时区分algorithm-only compute边界和包含fixture准备的
end-to-end候选边界。当前19201-line fixture的初始化、cache flush、CSR和
CPU数据路径不对称，因此PERF-001表中的20x/约94x不得依据本地候选
边界签收。xlsx也未给出`REPEAT_SET`值；runtime如实输出
`repeat_count=1 repeat_set_status=EXTERNAL_PERFORMANCE_PLAN`，冻结重复计划和门限
仍必须由IT性能计划外部闭环。六个PERF descriptor均声明
`HPU_REQ_PERF_BASELINE`：对应外部evidence必须冻结CPU baseline镜像/版本、
CPU与HPU的统一cycle边界、输入和精度、计数器语义，以及warmup、
`REPEAT_SET`和统计方法；`HPU_REQ_PERF_THRESHOLD`则独立关闭验收门限。
一次本地运行得到的cycle或speedup不能替代这两类证据。

首次克隆：

```bash
git clone --recurse-submodules https://github.com/mmmsssttt404/IT-SCPU-TestCases.git
cd IT-SCPU-TestCases
```

已有 checkout：

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

## 宿主机构建与回归

```bash
cmake -S . -B build/host -DHPU_ENABLE_INLINE_ASM=OFF
cmake --build build/host -j
ctest --test-dir build/host --output-on-failure -j
```

这会生成 49 个独立宿主机 executable 到 `build/host/cases/`，不是仅生成
object 文件。具有完整实现的 testcase 在自检查正确时由 CTest 直接判定通过；
CMB015 因算法契约缺失保持显式 blocked/预期失败。宿主机自检查证明 testcase、
oracle 和数据边界检查自洽。

如需同时构建上游 generator/encoder/reference：

```bash
cmake -S . -B build/all
cmake --build build/all -j
cmake --build build/all --target hpu_it_inline_asm_tools -j
ctest --test-dir build/all --output-on-failure
```

权威测试点表也有独立的一致性门禁（仅使用Python标准库）：

```bash
scripts/validate_testpoint_table.py /path/to/HPU-IT测试点分解.xlsx
```

默认要求工作簿SHA-256为
`a483d9b10c8626ced01d9e3cad55ce044e1884980b6c1cc9390d35737129f83d`，并逐项
核对49个case ID、46个testpoint、标题、类型、优先级和七类目录归属。

## Nexus-AM RISC-V ELF

需要 `riscv64-linux-gnu-gcc/binutils` 和
[`OpenXiangShan/nexus-am`](https://github.com/OpenXiangShan/nexus-am)：

```bash
export AM_HOME=/path/to/nexus-am
HPU_IT_MEM_BASE=0x87000000 HPU_IT_MEM_LINES=19201 JOBS=8 \
  scripts/build_nexus_am.sh build-nexus-am
scripts/validate_artifacts.sh build-nexus-am/images/latest
```

构建按 `HPU_IT_MEM_BASE`、`HPU_IT_MEM_LINES` 和源码内容指纹写入独立的
`build-nexus-am/images/hpu-*/` 目录；49 组 `.elf`、`.bin`、`.txt` 全部构建并
验证成功后，才原子更新 `build-nexus-am/images/latest` 符号链接。验证脚本默认
也会从 `images/latest` 解析当前完整产物，复算当前源码 fingerprint，并核对
目录 tag、memory sidecar、source-fingerprint、AM revision 和 toolchain identity。
每个 ELF 都必须内嵌一致的 memory profile 与 source fingerprint；验证器还检查
ELF64 RISC-V、入口 `0x80000000`、DRAM LOAD segment、关键指令字，并分别从
对应 ELF 重新生成 `.bin` 和 `.txt` 做全量逐字节比较。旧 profile 目录和旧版
平铺产物不会被删除。

向 IT 环境交付时，只复制 `readlink -f build-nexus-am/images/latest` 解析出的整个
fingerprint 目录，不要复制 `images/` 根目录中的旧平铺文件，也不要混合不同
fingerprint 目录内的三件套。

### GitHub Release 自动发布

仓库中的 `.github/workflows/hpu-it-release.yml` 在 GitHub Release 发布时自动：

1. checkout Release 对应的 tag，而不是默认分支的最新提交；
2. 使用固定的 `0x87000000/19201` HPU memory profile 构建并验证49个基线用例；
3. 从中筛选计算指令、组合指令序列和性能三类共27个可交付用例，明确排除
   CMB015；
4. 生成带 source fingerprint 的 `.tar.gz` IT 包及其 `.sha256`，上传到该
   GitHub Release。

工作流使用 `release: published`，因此正式 Release 和 prerelease 都会触发。
如果发布任务需要重试，可在 Actions 页面手动运行 `Publish HPU IT package` 并
输入已存在的 Release tag；同名资产会由重新验证的产物替换。工作流文件必须先
存在于仓库默认分支，GitHub 才会为后续 Release 触发它。

本地可以使用同一个打包入口：

```bash
export AM_HOME=/path/to/nexus-am
tests/hpu-it/scripts/build_nexus_am.sh "$AM_HOME/tests/hpu-it/build"
tests/hpu-it/scripts/package_release.sh \
  "$AM_HOME/tests/hpu-it/build/images/latest" \
  "$AM_HOME/tests/hpu-it/build/release"
```

压缩包内包含27套 `.elf/.bin/.txt`、适用用例的 DMA manifest、memory profile、
源码/AM/toolchain 指纹、qualification 状态和包内 `SHA256SUMS`；Release 同时
提供整个压缩包的独立 SHA-256 文件。

生产快照发布到带 source fingerprint 的不可变目录；每套包含ELF/bin/txt、
内嵌profile/fingerprint和对应DMA sidecar。运行结果由程序的golden、guard、
FAULT和timeout检查直接决定为`PASS`或`FAIL`，构建脚本再验证ELF64 RISC-V、
入口地址、加载布局、指令编码和sidecar一致性。

也可以由 CMake 调用：

```bash
cmake -S . -B build-rv \
  -DHPU_ENABLE_INLINE_ASM=OFF \
  -DHPU_NEXUS_AM_HOME=/path/to/nexus-am
cmake --build build-rv --target hpu_it_nexus_images -j
```

## IT 执行边界

目标环境是
[`mmmsssttt404/IT-SCPU-RTL`](https://github.com/mmmsssttt404/IT-SCPU-RTL)
（当前 URL 会跳转到维护者仓库）。IT 侧必须用相同的
`0x87000000/19201` HPU memory profile 构建 HPU Spike，并通过
Difftest runner 传入单个 testcase ELF。

本仓库提供从产物 `.hpu-mem-profile` 读取并传递 profile 的调用封装：

```bash
scripts/run_it_case.sh /path/to/IT-SCPU-RTL \
  build-nexus-am/images/latest/HPU_IT_DIR_INS_C0_001.elf \
  /absolute/path/from-external-monitor.evidence
```

guest 输出正确且返回码为0时直接报告`PASS`。可选的bind/UVM/monitor evidence
用于辅助定位和留档，不再把成功结果降级为第三种状态。

完整 VCS/Vivado Difftest 仍依赖服务器侧仿真器、Difftest 基础 Makefile、
release package 和授权；这些不随 testcase 仓库分发。尚未实现的CMB015继续
显式返回失败并在产物状态表中标记为blocked，不会被错误判为`PASS`。

IT 仓库同时提供 `tools/hpu/verilator-fallback/` 开源功能 smoke。该入口会固定
并校验 Verilator/Yosys 依赖，使用完整 `SimTop/LNSystem`、真实 `dma_sim.v`
以及受约束的 SRAM/BUFGCE 功能模型；它已用于复现 CSR round-trip。由于构建
固定为 `CONFIG_NO_DIFFTEST`、`--no-timing`，它只能做本地 bring-up，不能替代
VCS、逐指令 Difftest、Xilinx/TSMC 时序模型或正式签核。

完整 RTL 冒烟曾依次暴露四个集成缺陷：HPU DDR `AxCACHE=0000`令32-byte beat
误入Device bridge、custom1 raw位域未转换为HPU semantic command，以及
`PSYNC 0x7000000b`被重叠的DASICS CALL_J模式译成非法跳转。当前IT RTL已分别
固定DDR `AxCACHE=0011`、加入custom1 precode，并让HPU custom0译码优先且
DASICS检查受最终译码结果约束；新的`DecodeUnit.sv`由Chisel源码以原195端口
机器生成。第四项是modulus-table DLOAD的`extmem_done_pulse`早于对象V-set，
旧controller过早清除pending，导致`table_ready`无法置位；现在只在匹配对象的
`mod_table_load_done`形成时清pending，三个交付/native副本和manifest已同步。
这个HPU产品profile明确由HPU占用完整custom0/custom1空间，因此重叠编码的
DASICS CALL_J/JR不可达；如果产品必须同时保留DASICS，只能重新分配opcode或
增加互斥的elaboration配置，不能靠运行时CSR无歧义区分。

三项修复组合后，正式`0x87000000`默认CBO和no-CBO诊断ELF都在完整开源RTL
模型中严格得到唯一`HPU_SMOKE_BEGIN/HPU_SMOKE_OK`，零FAIL、FAULT、AXI
size或Verilator断言；监视链路覆盖DLOAD、DSTORE、PSYNC、CDC、IRQ与latch，
默认CBO版本还通过输出、输入保持和guard检查。这个结果关闭了最小一条line
DMA回环的bring-up阻塞，但不等于49个正式用例、逐指令Difftest、VCS或时序
签核通过。旧`0x87000000/4096` ELF、缺少独立尾部 guard 的
`0x87000000/18945`中间产物，以及复用 master secret-key 区作scratch的
`0x87000000/19009`中间产物均与当前 FHE 布局不兼容，已过期且不得交付；
必须重新构建并验证`0x87000000/19201`产物。各用例的monitor/cache等需求位
继续作为附加观测提示，不改变golden自检产生的`PASS`或`FAIL`。

最终19201-line standalone功能镜像还使用非`DIFFTEST`的HPU Spike逐项执行了
`CMB_004`、`CMB_010`、`CMB_012`、`PERF_001`、`PERF_005`和`PERF_006`，六项均通过
HTIF返回0；单项墙钟时间约1.7至3.0秒。该回归真实执行完整HPU指令流与全窗口
golden/guard检查，检查正确时直接报告`PASS`。它不额外提供RTL monitor、cache
总线证据或可签收的性能周期。带`DIFFTEST`宏构建的`spike`命令行不会作为这项
standalone回归入口；它只作为IT Difftest动态库构建的一部分使用。

上述独立模型回归已固化为脚本；无参数时构建并执行全部49项，也可只指定
一个或多个case ID。脚本会检查Spike的构建`config.log`，拒绝DIFFTEST模式或
与`0x87000000/19201`不一致的模型，并为每项生成独立ELF和运行日志：

```bash
AM_HOME=/path/to/nexus-am \
SPIKE=/path/to/non-difftest-cuihu2-spike \
  scripts/run_standalone_spike.sh HPU_IT_DIR_CMB_012
```

standalone 脚本在输出根目录加锁，先写入唯一的 `.staging-*` 目录；只有所有所选
case 的 ELF/bin/txt 和日志数量正确、运行均返回 0，且构建期间源码、Nexus-AM、
工具链及 Spike 身份均未变化时，才写 `.complete` 并原子重命名为最终目录。最终
目录名和 sidecar 绑定源码 fingerprint、AM revision、工具链、Spike 可执行文件及
`config.log` 的 SHA-256、脚本 SHA-256、ISA、case manifest/selection hash 与 run
fingerprint；脚本拒绝覆盖已有同名运行。它不维护可变的 standalone `latest`，
归档或交付时必须使用成功输出中打印的完整最终目录，不能复制 `.staging-*`。

standalone Spike 的 HPU 模型只有固定物理 aperture，没有 CSR MMIO/PLIC 设备。
因此 HTIF 专用镜像会把软件 shadow/commit 状态计算出的窗口相对 line 转换成
固定 aperture 的 backing line；生产 IT 镜像不做该转换，仍由真实 CSR active
window 完成地址重定位。每份 standalone 日志和最终目录中的
`.standalone-model-contract` 都记录
`active_window_translation=software csr_mmio=UNMODELED plic=UNMODELED`。
该模型可以验证 CFG_002 的 A/B 数据选择、隔离和重复 commit 无数据副作用，不能
作为 CSR 写入时序、原子切换、STATUS 或中断链路的资格证据。

该命令的`PASS`仅指standalone HPU功能模型自检查，不能产生
`HPU_IT_REQ_PASS`外部证据，也不能替代IT资格验收。

第四项修复除组件级时序TB外，又以默认CBO、`0x87000000`的完整开源RTL模型
执行了一条mod-table DLOAD→PMODLD→两对象DLOAD→PADD→DSTORE→PSYNC链。
修正仅存在于临时fixture的modctx写地址错误后，同一ELF严格得到一次
`HPU_PADD_BEGIN`和一次`HPU_PADD_OK`，零FAIL/FAULT/AXI或Verilator断言；
`q=0x10001`、`mu=0xffff0000ffff`，64个结果逐字等于golden且输入/guard不变。
日志SHA-256为`8829c12d1a69932b39c1e3b18c1f7c357f3b87b053091294201ab83cde5f8731`。
这只证明最小PADD数据链，不扩大上述49用例的正式验收范围。

当前外部闭环重点如下：

- CMB_001：BConv 软件闭环已完成；仍需在真实 RTL 保存 monitor/cache 证据。
- CMB_004：KeySwitch 已调用生成的 716-DMA 程序并完成逐条 relocation；仍需真实 RTL monitor/cache 证据。
- CMB_002/CMB_003：数据、Q4 relocation 和同输入 CPU reference 已接入；仍需
  在真实 RTL 保存 monitor/cache 证据。
- CMB_005/CMB_014：已冻结 Galois element 3，CPU 完成负循环 `X->X^3`
  重排，HPU 调用生成的 716-DMA Galois KeySwitch；输入、key、逐字 golden、
  guard 和 resolved manifest 均已闭环，仍需真实 RTL 证据。
- CMB_009：HADD已绑定为N=4096的PMODLD/PADD组合序列，使用确定性输入、
  独立CPU golden、poison目标和全窗口guard；仍需真实RTL命令/AXI/PSYNC记录。
- CMB_010：完整 ciphertext-multiply/HMUL 调用生成的 1197-DMA 程序，冻结序列
  仅末尾一次 PSYNC；仍需真实 RTL monitor/cache 证据及 xlsx 规格修订。
- CMB_012：N=4096、Q4/P3、D2 RelinOnly 调用生成的 729-DMA 程序，复用确定性
  tensor/评估密钥、逐阶段 golden、poison 输出和全窗口检查；仍需真实 RTL 证据。
- CMB_011：执行生成的 171 条 Encode 指令和 61 条 DMA relocation；输入为
  host signed-to-RNS 的 Q4 系数域明文，逐 limb 完成 pre-twist 与 12-stage NTT，
  输出按 4x4096 uint32 NTT-domain golden 逐字检查。
- CMB_013：执行生成的 169 条 Rescale 指令和 78 条 DMA relocation；对两个
  Q4 ciphertext component 加 `floor(q_last/2)` 后复用 ModDown，输出按
  2x3x4096 uint32 Q' golden 逐字检查。
- CMB_015：仍缺 bootstrap stage/key/scale 表；产物明确标记
  `BLOCKED_EXTERNAL_ALGORITHM_CONTRACT`，不发 HPU 命令。
- STR/STING：补双通道 monitor、ready/backpressure 驱动、覆盖率和 seed 回放。
- PERF_002/PERF_003：同输入 Q4 CPU baseline 与 cycle 边界已实现，仍缺冻结门限。
- PERF_004：复用CMB_001的N=4096、Q2↔P1完整BConv，同输入CPU reference、
  HPU程序、golden和cycle边界已实现；仍缺性能计划冻结门限。
- PERF_001/PERF_005/PERF_006：RelinOnly/KeySwitch/密文乘法软件/host闭环、
  CPU oracle和候选cycle边界已完成；仍缺冻结`REPEAT_SET`、对称测量方法、
  真实RTL计数器证据和性能门限签收。
- APP_001：已执行生成的完整 ciphertext-multiply/relinearization 程序并逐字自检；
  正式关闭仍需要目标 RTL 运行和应用性能门限证据。

第三方固定版本和许可证状态见 `THIRD_PARTY_NOTICES.md`。
