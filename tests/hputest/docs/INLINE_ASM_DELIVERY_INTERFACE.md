# inline-asm 到 Nexus-AM 的 HPU 用例接口

本文说明 [`cuihu2/inline-asm`](https://github.com/cuihu2/inline-asm)
如何生成数据、表格和指令，以及 Nexus-AM `tests/hputest` 如何实际接收并链接
这些内容。这里描述的是当前已经接通的 MM 冒烟路径，不是未来接口草案。

## 1. 版本与目录

生产者以 git submodule 固定在：

```text
tests/hputest/third_party/inline-asm
branch encode
commit 80b4725db7403e7a8a8663ed1344309e058f6a46
```

源码仓库只提交 submodule gitlink、接收脚本和测试源码。以下内容均由构建生成并
被 `.gitignore` 排除：

```text
third_party/inline-asm/{build,output,outputs}/
tests/hputest/build/
```

因此 GitHub 仓库不会被 ELF、BIN、数据镜像或 4096 项数组撑大；GitHub Actions
把选中的交付包和测试产物作为 artifact 提供下载。

## 2. 生产者如何生成

接收脚本构建并依次运行生产者的指令生成、编码和 reference-vector
三个阶段：

```bash
cmake -S tests/hputest/third_party/inline-asm \
      -B tests/hputest/build/inline-asm-cmake \
      -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build tests/hputest/build/inline-asm-cmake \
      --target inline_asm_codegen inline_asm_encode_outputs \
               hpu_reference_vectors --parallel 4
```

脚本在 submodule 根目录执行这三个可执行文件，产生 `output/`、
`outputs/` 和 MM 数据表；然后由 Nexus-AM importer 逐字段检查选中的
MM 契约。这使接收端不依赖未使用算子的整包交付流程。

当前 Nexus-AM 选择 `outputs/mm`，因为它同时满足：

- `N=4096`；
- 一个 RNS 模数 `q=50061313`；
- 两个 4096×`uint32_t` 输入；
- 一个 4096×`uint32_t` 点乘 golden；
- 一行 modulus context；
- 已完成 `x10/x11` relocation 的可执行 C 函数。

生产者还会生成其他算子和 twiddle。Nexus-AM 不把整个大镜像链接进
ELF，只严格选择 MM 冒烟所需的四个数据文件和一个程序。

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

路径前缀均为：

```text
outputs/mm/test_data/hardware/
```

每个 `.u32.bin` 的人工可读伴随文件由 `hardware_manifest.csv` 的
`readable_path` 字段指定；当前 `encode` 分支生成 `.u32.dec.txt` 十进制文本。
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

Nexus-AM 不把 4096 个数展开成不可读的 C 数组。接收层使用 `.incbin`：

```asm
hpu_rns_input_a:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/images/input_a.u32.bin"
```

`src/common/hpu_rns_fixture.S` 只放 A/B，并链接到 7 个 smoke-001 用例和
49 个迁移 IT 用例；两个 `main return` 探针刻意不带数据。
`src/common/hpu_mm_fixture.S` 放 expected/mod_ctx，仅链接到计算用例 07。

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

其中 `.inst32`/`.cmd26` 是 decode 对照文本，不直接执行。Nexus-AM 真正链接的是
`mm.c`，入口为：

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
```

生成函数内的顺序是：

```text
DLOAD mod -> PMODLD 0 -> DLOAD A -> DLOAD B -> PMUL
-> PFREE inputs -> DSTORE output -> PFREE mod -> terminal PSYNC
```

完整程序只发一条末尾 PSYNC；用例 C 不再重复发 PSYNC。

## 6. x10/x11 到底怎么传

custom1 指令字只编码 rs1/rs2 的寄存器编号，不编码 offset/count 数值。运行时 ABI：

```text
GPR[x10] = line_offset
GPR[x11] = line_count
单位       = 256-byte HPU line
```

`dma_relocation_manifest.csv` 必须逐条写明 custom1 的 instruction index、DMA
index、方向、对象、word、`rs1=x10` 和 `rs2=x11`。当前 MM 有四条 DMA。

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

01–06 和迁移 IT 中的基础指令用例需要独立
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
-> compile testcase + selected producer mm.c/data
-> validate ELF symbols/instruction words/bin/disassembly
-> package artifact
```

push 到 `master` 或手动触发时，GitHub Actions 分别发布
`nexus-am-hpu-core-workloads`、`nexus-am-hpu-transform-workloads` 和
`nexus-am-hpu-fhe-workloads`；PR 只构建较快的 `core` 组。每个 artifact 保留
7 天并包含该组的：

- 分组目录中的 ELF/BIN/TXT（完整三组共 58 个用例）；
- `MANIFEST.txt`、`CASE_MANIFEST.tsv` 和 `NOT_QUALIFIED.tsv`；
- `provenance/inline-asm-mm/`：选中的 bin/readable/table/mm.c/mm.h/mm.asm、producer commit、
  resolved spans 和 summary。

生成物不进入 Git history。

## 9. 必须拒绝的情况

接收脚本遇到以下任一情况返回非零：

- submodule 未初始化、有 tracked 修改或版本不符；
- 缺少 MM C/H、relocation manifest、params、ABI、line map 或数据文件；
- N/q/shape/byte order/line geometry 改变；
- FNV、文件长度、mod_ctx 或 4096 项 golden 不匹配；
- DMA 不是四条、rs1/rs2 不是 x10/x11、存在 `x0,x0` placeholder；
- MM 指令流不是经过审查的十条或缺少唯一末尾 PSYNC；
- 选中的 MM producer 数据越过其 256-line 接收窗口；
- ELF 未嵌入正确尺寸的数据符号，或反汇编缺少 producer 指令字；
- output 被预装成 golden。

## 10. 当前边界

当前 producer 的完整程序闭环只覆盖 `MM/PMUL, N=4096, Q=1`；迁移 IT 中的基础
CSR、DMA 和算术用例使用同一 producer 的 A/B 与编码器输出。缺完整 N=4096
program/data/golden/relocation 契约的 24 个测试点被标成
`blocked-not-issued`，不发明指令并固定返回 1。GitHub Actions 的绿色结果也只证明
生成、导入、编译和静态产物校验通过，不等于外部 IT/VCS 仿真已经 PASS。VCS
失败记录应保留为外部证据，不在 Nexus-AM 或 RTL 中猜测修复。
