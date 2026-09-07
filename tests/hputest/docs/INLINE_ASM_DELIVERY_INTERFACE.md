# inline-asm 到 Nexus-AM 的 HPU 用例接口

本文说明 [`cuihu2/inline-asm`](https://github.com/cuihu2/inline-asm)
如何生成数据、表格和指令，以及 Nexus-AM `tests/hputest` 如何实际接收并链接
这些内容。这里描述的是当前已经接通的 MM 冒烟路径，不是未来接口草案。

## 1. 版本与目录

生产者以 git submodule 固定在：

```text
tests/hputest/third_party/inline-asm
branch main
commit 04d1825bdbce4dc649a683a722e59cf100c1a686
```

`.gitmodules` 中的分支名记录上游来源；本地构建和 CI 均使用 Nexus-AM 提交中
固定的 gitlink，不自动追踪远端分支最新提交。

当前引用上游 `main` 的“修复 ntt dma 的 encode 问题”提交，接收其 STG 与 DMA
编码修正。AM 不再引用 `HPU_SEAL_manual_0905` 试验分支；该分支及原
`HPU_SEAL` 保留为历史来源，本次不修改它们。

源码仓库只提交 submodule gitlink、接收脚本和测试源码。以下内容均由构建生成并
被 `.gitignore` 排除：

```text
tests/hputest/build/inline-asm-producer/<producer_commit>/
tests/hputest/build/generated/
tests/hputest/build/artifact/
```

上述目录的默认根目录为 `OUTPUT_ROOT=tests/hputest/build`，已校验交付目录默认
为 `HPU_GENERATED_ROOT=OUTPUT_ROOT/generated`。生产者工具不在 submodule 源码
目录中生成或覆盖交付文件。

因此 GitHub 仓库不会被 ELF、BIN、数据镜像或 4096 项数组撑大；GitHub Actions
把选中的交付包和测试产物作为 artifact 提供下载。

## 2. 生产者如何生成

接收脚本构建并依次运行生产者的指令生成、编码和 reference-vector
三个阶段：

```bash
git submodule update --init --recursive tests/hputest/third_party/inline-asm
make -C tests/hputest prepare-inline-asm-mm JOBS=4
```

该 target 调用 `scripts/prepare-inline-asm-mm.sh`，构建生产者的
`inline_asm_codegen`、`inline_asm_encode_outputs` 和 `hpu_reference_vectors`。
生成前还会运行生产者的 `hpu_encode_self_test`，覆盖固定 STG/DMA 等机器码、
26-bit precode 及可执行 C 中的 `.word`。当前上游自测不是此前试验分支的
16,384 组 STG 全字段穷举，不能继续把那个测试数量当作本版本的验证结果。
三个可执行文件在 `OUTPUT_ROOT/inline-asm-producer/<producer_commit>/` 中运行，
产生该工作目录下的 `output/`、`outputs/` 和 MM 数据表；Nexus-AM importer
逐字段检查选中的 MM 契约，再将交付内容导入
`HPU_GENERATED_ROOT/inline-asm/mm`。这使接收端不依赖未使用算子的整包交付流程，
也不复用 submodule 源码目录中可能残留的旧生成数据。
构建配置显式关闭 `HPU_ENABLE_SEAL_DIFFERENTIAL_ORACLE` 及其兼容别名
`HPU_ENABLE_SEAL_BFV_ORACLE`；差分 oracle 不属于本次 MM 数据生成依赖。
当前 `main` 已无原 SEAL integration 和 legacy profile 开关，接收端不再传入它们。

当前 Nexus-AM 选择 `outputs/mm`，因为它同时满足：

- `N=4096`；
- 一个 RNS 模数 `q=50061313`；
- 两个 4096×`uint32_t` 输入；
- 一个 4096×`uint32_t` 点乘 golden；
- 一行 modulus context；
- 已完成 `x10/x11` relocation 的可执行 C 函数。

生产者还会生成其他算子和 twiddle。Nexus-AM 不把整个大镜像链接进
ELF，只严格选择 MM 冒烟所需的四个数据文件和一个程序。
引用 `main` 不等于启用完整 SEAL/CKKS 应用；当前接入仍限于上述固定
4096 系数、1 RNS 分量的 MM 路径。

## 3. 数据文件和人工可读表格

HPU 数据 ABI 为：

```text
element       uint32_t
byte order    little-endian
line size     256 bytes
words/line    64
4096 words    16384 bytes = 64 lines
line offset   relative to configured HPU_MEM_BASE
```

当前选中的文件为：

| 生产者文件 | 作用 | shape | Nexus line range |
|---|---|---:|---:|
| `images/input_a.u32.bin` | 输入 A | 4096 | `[0,64)` |
| `images/input_b.u32.bin` | 输入 B | 4096 | `[64,128)` |
| `images/expected.u32.bin` | PMUL golden | 4096 | `[128,192)` |
| `constants/mod_ctx.u32.bin` | q/mu48 记录 | 1×4，补零到1 line | `[192,193)` |

这些路径在生产者隔离工作目录中的前缀为：

```text
outputs/mm/test_data/hardware/
```

校验导入后，对应文件位于
`HPU_GENERATED_ROOT/inline-asm/mm/test_data/hardware/`。

每个 `.u32.bin` 的人工可读伴随文件由 `hardware_manifest.csv` 的
`readable_path` 字段指定；当前固定版本的 MM 路径生成 `.u32.dec.txt` 十进制文本。
接收端不再猜测或写死展示文件后缀。生产者还给出：

- `test_data/params.json`：N、operation、domain、模数；
- `test_data/hardware/line_map.csv`：path、shape、offset、count、字节数；
- `test_data/hardware/hardware_manifest.csv`：每个 image 的 FNV-1a；
- `test_data/hardware/mod_ctx_map.csv`：q 与 Barrett mu48；
- `test_data/hardware/abi.json`：字节序、line 和 custom1 sideband 规则。

接收脚本不会信任文件名就直接编译。它会检查：

1. N、q、little-endian uint32 和 256-byte line；
2. 四个 region 的 offset/count/size；
3. 文件 FNV 与 hardware manifest；
4. A、B、golden 全部 4096 项均为 canonical residue；
5. 每项 `golden[i] == (uint64_t)A[i] * B[i] % q`；
6. modulus context 为 `{q, mu_lo, mu_hi16, 0}` 且其余 padding 为零。

## 4. 数据如何进入 testcase ELF

Nexus-AM 不把 4096 个数展开成不可读的 C 数组。接收层使用 `.incbin`，且只读取
已校验导入目录中的数据。例如 `RNS_A` 对应：

```text
HPU_GENERATED_ROOT/inline-asm/mm/test_data/hardware/images/input_a.u32.bin
```

汇编源保持简短的相对路径：

```asm
RNS_A:
    .incbin "images/input_a.u32.bin"
```

`Makefile.case` 通过 `-Wa,-I$(INLINE_ASM_MM_ROOT)/test_data/hardware` 指定 GNU as
的搜索目录，其中 `INLINE_ASM_MM_ROOT` 指向已校验的
`HPU_GENERATED_ROOT/inline-asm/mm`，而不是 submodule 的 `outputs/mm`。

`src/common/hpu_rns_fixture.S` 只放 A/B，并链接到 9 个 smoke-001 用例和
49 个迁移 IT 用例；两个 `main return` 探针刻意不带数据。
`src/common/hpu_mm_fixture.S` 放 expected/mod_ctx，仅链接到计算用例 08、09。

运行时：

- A/B/mod_ctx 从 ELF 只读区复制到 HPU window；
- output `[128,192)` 先写入 poison，再 cache clean；
- immutable golden 留在 ELF 中，绝不预装到 output；
- PSYNC 完成后 invalidate output，逐项与 C oracle 和 producer golden 比较。

这样 HPU 没有执行 DSTORE 时不可能因为 output 预先等于 golden 而假通过。

## 5. HPU 指令如何传入

生产者生成：

```text
outputs/mm/mm.asm
outputs/mm/mm.inst32
outputs/mm/mm.cmd26
outputs/mm/dma_relocation_manifest.csv
outputs/mm/mm.h
outputs/mm/mm.c
```

其中 `.inst32`/`.cmd26` 是decode对照文本。原始producer入口是：

```c
int hpu_program_mm(const hpu_dma_span_t *spans, size_t span_count);
```

用例 C 清楚地给出四个 span：

```c
static const hpu_dma_span_t mm_spans[HPU_PROGRAM_MM_DMA_COUNT] = {
    {HPU_LINE_MOD, 1U},
    {HPU_LINE_SRC_A, HPU_RNS_LINES},
    {HPU_LINE_SRC_B, HPU_RNS_LINES},
    {HPU_LINE_OUTPUT, HPU_RNS_LINES},
};

if (mm_load_mod(mm_spans, HPU_PROGRAM_MM_DMA_COUNT) != 0) return 1;
psync();
if (completion_wait() != 0 || completion_clear() != 0) return 1;
if (mm_compute(mm_spans, HPU_PROGRAM_MM_DMA_COUNT) != 0) return 1;
if (completion_wait() != 0 || completion_clear() != 0) return 1;
```

2026-09-05手册0.4要求模表装载后先同步。AM接收脚本保留原始`mm.c`用于追踪，
另生成并链接`mm_phases.c/h`，把第一笔DLOAD和后续计算拆开。用例中的顺序是：

```text
DLOAD mod -> PSYNC -> 等待并清完成通知 -> PMODLD 0 -> DLOAD A -> DLOAD B -> PMUL
-> PFREE inputs -> DSTORE output -> PFREE mod -> terminal PSYNC
```

完整MM用例有两次PSYNC。08通过中断处理并在阶段间`irq_rearm()`；09使用上述MMIO等待。
两份阶段函数不改producer原机器码或x10/x11绑定。阶段不应独立乱序调用。

## 6. x10/x11 到底怎么传

custom1 指令字只编码 rs1/rs2 的寄存器编号，不编码 offset/count 数值。运行时 ABI：

```text
GPR[x10] = line_offset
GPR[x11] = line_count
单位       = 256-byte HPU line
```

`dma_relocation_manifest.csv` 必须逐条写明 custom1 的 instruction index、DMA
index、方向、对象、word、`rs1=x10` 和 `rs2=x11`。当前 MM 有四条 DMA。

当前 `main` 的 32-bit DMA 字段是：

```text
inst32 = (rs2 << 27) | (rs1 << 22) | (flag << 17) | (obj << 10)
       | (operation << 8) | (dir << 7) | 0x2B
DLOAD: dir=0, operation=type
DSTORE: dir=1, operation=rel << 1
custom0 cmd26 = inst32 >> 7
custom1 cmd26 = (1 << 25) | (inst32 >> 7)
```

cmd26 不再丢弃 GPR 编号后重排 DMA 位段，必须保留 `inst32[31:7]`。
当前 MM 的四笔 DMA 固定为：

| DMA | inst32 | cmd26 | line offset/count |
| --- | --- | --- | --- |
| 模表 DLOAD | `0x5A820E2B` | `0x2B5041C` | 192 / 1 |
| 输入 A DLOAD | `0x5A80052B` | `0x2B5000A` | 0 / 64 |
| 输入 B DLOAD | `0x5A80092B` | `0x2B50012` | 64 / 64 |
| 输出 DSTORE（rel=1） | `0x5A8002AB` | `0x2B50005` | 128 / 64 |

这是指令字的位段更新，不是运行时 offset/count ABI 的变化。旧 ELF/BIN 不会随
submodule 更新自动改变，IT 复测必须替换成重新构建的文件。

生产者生成的 `mm.c` 在每条 custom1 前都执行等价代码：

```c
register uintptr_t hpu_rs1 __asm__("x10") = spans[dma].line_offset;
register uintptr_t hpu_rs2 __asm__("x11") = spans[dma].line_count;
__asm__ volatile(".word 0x..."
                 : : "r"(hpu_rs1), "r"(hpu_rs2) : "memory");
```

这保证 HPU 指令执行点的 `x10/x11` 值正确。编译器可能复用前一条相同的
`x11=64`，所以反汇编不保证每条 custom1 前都出现一条独立的文本 `li x11,64`；
验收的是 custom1 执行点寄存器值，不是伪指令的外观。如将来必须固定每条 `li`
形态，生产者应改为生成 `.S`，而不是由消费者手写。

## 7. 简单用例的单指令适配

01–07 和迁移 IT 中的基础指令用例需要独立
DLOAD/DSTORE/PMODLD/算术/PFREE/PSYNC，而不是完整 MM 程序。它们使用的 named
word 也不再手写在 Git 中。`prepare-inline-asm-mm.sh` 编译生产者的真实 encoder，
把 mnemonic 编成 build-only header：

```text
tests/hputest/build/generated/include/hpu/inline_asm_mm_delivery.h
```

tracked `include/hpu/encoding.h` 只 include 这个生成头文件。这样每个 `main.c`
保持“一条 HPU 操作对应一个可见调用”的可读结构，同时编码仍来自同一个
producer。

## 8. 构建和 GitHub Actions

本地完整流程：

```bash
git submodule update --init --recursive tests/hputest/third_party/inline-asm
make -C tests/hputest prepare-inline-asm-mm
make -C tests/hputest \
  ARCH=riscv64-xs CROSS_COMPILE=riscv64-linux-gnu-
```

构建顺序固定为：

```text
producer instruction/data generation stages
-> validate/import selected MM files
-> generate encoder header
-> generate AM mm_phases.c/h from reviewed producer mm.c
-> compile testcase + AM phases + selected producer data
-> validate ELF symbols/instruction words/bin/disassembly
-> package artifact
```

push 到 `master` 或手动触发时，GitHub Actions 分别发布
`nexus-am-hpu-core-workloads`、`nexus-am-hpu-transform-workloads` 和
`nexus-am-hpu-fhe-workloads`；PR 只构建较快的 `core` 组。每个 artifact 保留
7 天并包含该组的：

- 分组目录中的 ELF/BIN/TXT（完整三组共 60 个用例）；
- `MANIFEST.txt`、`CASE_MANIFEST.tsv` 和 `NOT_QUALIFIED.tsv`；
- `provenance/inline-asm-mm/`：选中的 bin/readable/table/mm.c/mm.h/mm.asm、AM派生mm_phases.c/h、producer commit、
  resolved spans 和 summary。

生成物不进入 Git history。

## 9. 必须拒绝的情况

接收脚本遇到以下任一情况返回非零：

- submodule 未初始化、有 tracked 修改或版本不符；
- 缺少 MM C/H、relocation manifest、params、ABI、line map 或数据文件；
- N/q/shape/byte order/line geometry 改变；
- FNV、文件长度、mod_ctx 或 4096 项 golden 不匹配；
- DMA 不是四条、rs1/rs2 不是 x10/x11、存在 `x0,x0` placeholder；
- 原始producer MM指令流不是经过审查的十条或缺少末尾PSYNC；AM派生阶段不能逐字保留原指令；
- 选中的 MM producer 数据越过其 256-line 接收窗口；
- ELF 未嵌入正确尺寸的数据符号，或反汇编缺少 producer 指令字；
- output 被预装成 golden。

## 10. 当前边界

当前 Nexus-AM 接收的完整程序闭环只覆盖 `MM/PMUL, N=4096, Q=1`，不代表
`main` 分支只支持这一种程序，也不代表其完整 SEAL/CKKS 流程已经接入。
迁移 IT 中的基础
CSR、DMA 和算术用例使用同一 producer 的 A/B 与编码器输出。缺完整 N=4096
program/data/golden/relocation 契约的 24 个测试点被标成
`blocked-not-issued`，不发明指令并固定返回 1。GitHub Actions 的绿色结果也只证明
生成、导入、编译和静态产物校验通过，不等于外部 IT/VCS 仿真已经 PASS。VCS
失败记录应保留为外部证据，不在 Nexus-AM 或 RTL 中猜测修复。
06、07、08 已有 IT 失败反馈；本次接收上游编码修正后仍需重新运行这些用例，
没有新一轮日志和波形前，不将其标为 PASS，也不把编码修正认定为全部失败的根因。

STG 编码按用户指定的 2026-09-05 手册 §3.2 字段公式执行：`pdata` 同时写入
`[27:25]` 和 `[24:22]`，`ptwid` 写入 `[16:14]`，`[21:17]` 为 0。
旧示例的机器码不再作为预期值；接收端用独立的手册向量检查 word/cmd26。
这仅解决指令位段，不补齐完整变换用例的 program/data/golden/relocation 契约；
因此相关用例继续保持 blocked，不能将编码校验通过等同于 IT/VCS 功能通过。
