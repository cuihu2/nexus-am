# inline-asm 到 Nexus-AM 的 HPU 用例接口

本文说明 [`cuihu2/inline-asm`](https://github.com/cuihu2/inline-asm)
如何生成数据、表格和指令，以及 Nexus-AM `tests/hputest` 如何实际接收并链接
这些内容。这里描述的是当前已经接通的 MM 冒烟路径，不是未来接口草案。

## 1. 版本与目录

生产者以 git submodule 固定在：

```text
tests/hputest/third_party/inline-asm
branch main
commit b405f2ad7b0901930d81edb79a5167ab028c4dbd
```

`.gitmodules` 中的分支名记录上游来源；本地构建和 CI 均使用 Nexus-AM 提交中
固定的 gitlink，不自动追踪远端分支最新提交。

当前引用上游 `main` 的上述固定提交，接收其 STG 与 DMA 编码、生成程序和
编程约定更新；规范来源为该提交中的 `doc/HPU_PROGRAMMING_MANUAL.md`，
不能仅用仍标作 v0.4 的文档标题判断内容相同。
AM 不再引用 `HPU_SEAL_manual_0905` 试验分支；该分支及原
`HPU_SEAL` 保留为历史来源，本次不修改它们。

上述上游仍使用 HPU 主 opcode `0x0B`。按 IT 新接口要求，AM 在导入时将其
低 7 位统一映射为 `0x5B`，不修改上游 `main` 或固定 gitlink。指令 payload、
cmd26、DMA 编码和全部数据仍取该固定提交，具体映射见第 5 节。

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

其中 `.inst32`/`.cmd26` 是 decode 对照文本。AM 校验上游原始程序，再应用
主 opcode 映射，编译目标交付中的 `mm.c`。用例仍调用生产者同名入口：

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

if (hpu_program_mm(mm_spans, HPU_PROGRAM_MM_DMA_COUNT) != 0) return 1;
if (completion_wait() != 0 || completion_clear() != 0) return 1;
```

上例展示 09 的 MMIO 完成等待；08 在调用生产者程序前执行 `irq_open()`，
之后通过 `irq_wait()` 接收同一末尾事件。当前固定版本手册第 6.1、6.3 节
明确 DMA/计算依赖由硬件维护，完整程序只在末尾发一次 PSYNC。
这替代此前依据旧 PDF 添加模表屏障的适配。AM 直接编译已校验导入的 `mm.c`，
不再生成 `mm_phases.c/h`，不再拆分或插入指令。顺序为：

```text
DLOAD mod -> PMODLD 0 -> DLOAD A -> DLOAD B -> PMUL
-> PFREE inputs -> DSTORE output -> PFREE mod -> terminal PSYNC
```

完整 MM 用例只有一次 PSYNC，不调用 `irq_rearm()`。十条指令的顺序和
`inst[31:7]`、四笔 x10/x11 绑定、`hpu_obj_len` 软件生命周期检查均保持原样；
只有原 custom0 指令的低 7 位发生变化。接收端必须
校验 DSTORE 的 `span.line_count` 等于已建立对象的长度，不能删掉该检查来
迎合新的生成文件格式。

### 主 opcode 映射与追溯

物理 RISC-V opcode 从 custom0 `0x0B` 改到 custom2 `0x5B`，HPU 内部仍为
`cmd_kind=0`。源文件名和历史测试点中的 custom0/C0 只保留内部类别含义。
接收层仅对 `(old_inst & 0x7F) == 0x0B` 的 HPU 指令执行：

```text
new_inst = (old_inst & 0xFFFFFF80) | 0x5B
```

不改变 funct/对象/stage/mode/flag 等 payload 位，不改 custom1 `0x2B` 的
DLOAD/DSTORE，不增加、删除或重排任何指令。bit 7 不属于 opcode，必须保留，
所以 flag=1 的目标低字节为 `0xDB`，不是 `0x5B`。

| 指令字示例 | 上游 word | AM 目标 word |
| --- | --- | --- |
| PSYNC | `0x7000000B` | `0x7000005B` |
| PMODLD 0 | `0x6000000B` | `0x6000005B` |
| PADD 示例 | `0x0400400B` | `0x0400405B` |
| PFREE p0 | `0x8000000B` | `0x8000005B` |

`HPU_GENERATED_ROOT/inline-asm/mm/` 及产物中的
`provenance/inline-asm-mm/` 使用相同的追溯结构：

```text
mm.c / mm.inst32 / mm.cmd26 / encoder_words.tsv       # AM 目标交付
opcode_map.csv                                      # MM 逐条 word 映射
upstream/{mm.c,mm.inst32,mm.cmd26,encoder_words.tsv}   # 原始上游交付
```

目标 `mm.c` 的 `.word`、`mm.inst32` 及单指令头文件必须采用同一映射；
cmd26 因只依赖 `inst[31:7]` 而不变。`mm.asm` 的助记符、MM 数据、原始
relocation manifest 和四笔 DMA word 同样不变。保留原始文件是为了区分
生产者输出与 AM 物理 opcode 适配，不能将目标 C 称为“与上游逐字节相同”。

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
inst32 = (obj << 25) | (rs2 << 20) | (rs1 << 15)
       | (operation << 13) | (dir << 12) | (flag << 7) | 0x2B
DLOAD: dir=0, operation=type
DSTORE: dir=1, operation=rel << 1
HPU cmd_kind=0 cmd26 = inst32 >> 7  # 目标物理 opcode 为 custom2 0x5B
custom1 cmd26 = (1 << 25) | (inst32 >> 7)
```

cmd26 不再丢弃 GPR 编号后重排 DMA 位段，必须保留 `inst32[31:7]`。
当前 MM 的四笔 DMA 固定为：

| DMA | inst32 | cmd26 | line offset/count |
| --- | --- | --- | --- |
| 模表 DLOAD（p3） | `0x06B540AB` | `0x20D6A81` | 192 / 1 |
| 输入 A DLOAD（p1） | `0x02B5202B` | `0x2056A40` | 0 / 64 |
| 输入 B DLOAD（p2） | `0x04B5202B` | `0x2096A40` | 64 / 64 |
| 输出 DSTORE（p0，rel=1） | `0x00B5502B` | `0x2016AA0` | 128 / 64 |

这是指令字的位段更新，不是运行时 offset/count ABI 的变化。旧 ELF/BIN 不会随
submodule 更新自动改变，IT 复测必须替换成重新构建的文件。
CPU 取源寄存器现在使用标准位置 `[19:15]` 和 `[24:20]`；对象号、操作与标志
仍必须按上式解析，不能只因为 GPR 位置相同就使用更早一版的整条 DMA 编码。

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

DSTORE 的实际硬件长度取 `OBJ.len`，不取 `x11`；统一 ABI 仍填写非零
`span.line_count`，由生成函数在发射前核对其与软件跟踪的 `OBJ.len` 相等。
`rel=0` 与 `rel=1` 在当前约定中都释放对象，后续不得对同一份分配再次 PFREE。

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
producer。生成的 named word 在写入头文件前同样把 HPU opcode `0x0B` 映射
到 `0x5B`；不能只改完整 MM 程序而漏掉 PSYNC、PMODLD、PFREE 等单指令适配。

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
-> map HPU opcode 0x0B to 0x5B and preserve upstream provenance
-> generate opcode-mapped encoder header
-> compile testcase + opcode-mapped mm.c + selected producer data
-> validate ELF symbols/instruction words/bin/disassembly
-> package artifact
```

push 到 `master` 或手动触发时，GitHub Actions 分别发布
`nexus-am-hpu-core-workloads`、`nexus-am-hpu-transform-workloads` 和
`nexus-am-hpu-fhe-workloads`；PR 只构建较快的 `core` 组。每个 artifact 保留
7 天并包含该组的：

- 分组目录中的 ELF/BIN/TXT（完整三组共 60 个用例）；
- `MANIFEST.txt`、`CASE_MANIFEST.tsv` 和 `NOT_QUALIFIED.tsv`；
- `provenance/inline-asm-mm/`：选中的 bin/readable/table、目标 mm.c/mm.inst32、
  mm.h/mm.asm、producer commit、resolved spans、summary、`opcode_map.csv`，
  以及 `upstream/` 中保留的原始程序与编码表。

生成物不进入 Git history。

## 9. 必须拒绝的情况

接收脚本遇到以下任一情况返回非零：

- submodule 未初始化、有 tracked 修改或版本不符；
- 缺少 MM C/H、relocation manifest、params、ABI、line map 或数据文件；
- N/q/shape/byte order/line geometry 改变；
- FNV、文件长度、mod_ctx 或 4096 项 golden 不匹配；
- DMA 不是四条、rs1/rs2 不是 x10/x11、存在 `x0,x0` placeholder；
- 原始 producer MM 指令流不是经过审查的十条、PSYNC 不止末尾一次，
  或生成 C 的机器码、固定 GPR 绑定与软件对象长度检查不符合该契约；
- opcode 映射改变了 `inst[31:7]`、cmd26 或 DMA word，或目标生成 C、
  `.inst32`、单指令头文件与映射预期不一致；
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

STG 编码按固定版本手册 §3.2 字段公式执行：`pdata` 同时写入
`[27:25]` 和 `[24:22]`，`ptwid` 写入 `[16:14]`，`[21:17]` 为 0。
旧示例的机器码不再作为预期值；接收端用独立的手册向量检查 word/cmd26，
再核对目标低 7 位为 `0x5B`，其余字段保持不变。
这仅解决指令位段，不补齐完整变换用例的 program/data/golden/relocation 契约；
因此相关用例继续保持 blocked，不能将编码校验通过等同于 IT/VCS 功能通过。

IT 的 CPU 前端必须已将 custom2 `0x5B` 分类为 HPU 的 `cmd_kind=0`，
并保留 DMA custom1 `0x2B` 的当前译码；新 ELF 不能搭配仅识别 HPU `0x0B`
的旧 simv。本次 AM 适配不修改 CPU/HPU RTL，也不以软件映射宣称硬件已经更新。
