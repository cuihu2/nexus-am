# inline-asm 到 Nexus-AM 的 HPU 用例交付接口

本文定义 [`cuihu2/inline-asm`](https://github.com/cuihu2/inline-asm)
（下文称“生产者”）与 Nexus-AM `tests/hputest`（下文称“消费者”）之间的交付接口。
接口同时覆盖：

- RNS 输入、常量、中间结果和 golden 数据；
- 人工可读的数据表、地址表和指令表；
- HPU 助记符、32-bit 指令字和 26-bit decode 参考值；
- DLOAD/DSTORE 前用于设置 `x10`、`x11` 的普通 RISC-V 指令；
- Nexus-AM 如何把包嵌入 ELF、配置 HPU、执行和 self-check。

本文只定义软件交付边界，不声明任何 RTL/VCS 用例已经通过。

## 1. 当前状态和目标状态

Nexus-AM 当前固定使用 inline-asm commit：

```text
4399883b9e1fa249b99d48c7e919ee52acc662bc
```

当前双方尚未完成完整的数据和程序接入：

- Nexus-AM 目前只调用该 commit 的真实 encoder，核对 8 条
  `mnemonic -> inst32` 映射；
- `tests/hputest/src/common/hpu_rns_fixture.S` 中的两组 4096 系数仍由
  Nexus-AM 本地生成，不是从 inline-asm 数据包接收；
- inline-asm 已能生成数据镜像、line map、golden、ASM、`.inst32` 和
  `.cmd26`；
- 但主算子的大多数 DLOAD/DSTORE 仍写成 `x0, x0`，没有把数据表中的
  `line_offset`、`line_count` 绑定到实际 GPR；
- `.inst32` 只编码寄存器编号，`.cmd26` 只用于 decode 对照，两者都不携带
  `x10`、`x11` 在运行时应保存的数值。

因此接口分为两个版本：

| 版本 | 状态 | Nexus-AM 用法 |
|---|---|---|
| v1 | 当前已有的数据与 HPU 编码包 | 只能作为数据、编码和人工审查参考 |
| v2 | 本文定义的完整可执行交付包 | 校验通过后才可链接进 Nexus-AM ELF |

生产者必须在 `delivery.json` 中给出状态。消费者只自动接收
`status = "EXECUTABLE"` 的 v2 包；`DATA_ONLY`、`HPU_STREAM_ONLY`、
`PENDING_RELOCATION` 均不得当作可执行用例。

## 2. 双方职责

### 2.1 inline-asm 负责

1. 冻结算法参数：`N`、RNS 模数、seed、对象布局和期望输出。
2. 生成 little-endian `uint32` HPU 数据镜像及人工可读文本。
3. 生成每个数据对象的 line offset/count、shape、role 和校验值。
4. 生成 HPU 指令顺序、32-bit 指令字和 26-bit decode 参考值。
5. 为每条 DLOAD/DSTORE 绑定具体数据对象，并给出 `x10=line_offset`、
   `x11=line_count`。
6. 生成包含普通 RISC-V GPR 装载和 HPU `.word` 的 `program.S`。
7. 生成 producer commit、配置、schema version 和全部文件的 SHA-256。

生产者不能只交付 `dload x0, x0` 占位流，也不能要求消费者根据指令顺序
猜测某条 DMA 应对应哪个数据文件。

### 2.2 Nexus-AM 负责

1. 固定并验证 producer commit 和 v2 schema。
2. 选择本 IT 环境实际使用的 HPU window 绝对基址和容量。
3. 把生产者的相对 line offset 重定位到该 window；不得照抄生产者示例基址。
4. 把输入、常量和不可变 golden 链接进 ELF，并在运行时准备 DDR。
5. 对输出区域预填 poison，执行 cache clean/invalidate。
6. 按地址粒度写 BASE/SIZE shadow CSR，再写 COMMIT。
7. 调用生产者交付的程序入口，完成同步、fault 检查和逐系数 self-check。
8. self-check 成功时 `return 0`，任一错误 `return 1`；不依赖 UART。

Nexus-AM 不重新发明算法数据、模数、指令顺序或 DMA 绑定关系。

## 3. 生产者生成入口

当前 inline-asm 的统一生成入口为：

```bash
cmake -S . -B build
cmake --build build -j --target hpu_delivery
ctest --test-dir build --output-on-failure
```

它按顺序运行：

1. `inline_asm_codegen`：生成 `output/*.cpp` 和 `output/*.asm`；
2. `inline_asm_encode_outputs`：生成 `outputs/<case>/*.inst32/.cmd26`；
3. `hpu_reference_vectors`：生成数据、golden 和 hardware image；
4. delivery checker：检查现有交付文件。

当前算法参数仍分别硬编码在 `src/main.cpp` 和
`test/reference/main.cpp`。`params.json` 是生成结果，不是生成输入；修改输出的
JSON 不会改变下一次生成。因此 v2 必须把两处实际使用的配置摘要写进
`delivery.json`，并由生成器检查二者一致。

## 4. v2 交付包目录

每个可执行用例应生成一个自包含目录：

```text
outputs/<case>/
├── delivery.json                       # v2 总入口和 producer 身份
├── DELIVERY.md                         # 面向人的参数/数据/指令摘要
├── <case>.asm                          # HPU 助记符审查视图
├── <case>.inst32                       # 每行一个 32-bit HPU 指令字
├── <case>.cmd26                        # 每行一个 26-bit decode 参考值
├── program/
│   ├── program.S                       # Nexus-AM 实际链接的 RV+HPU 程序
│   └── instruction_manifest.csv        # 指令、数据对象和 sideband 的绑定表
├── test_data/
│   ├── params.json
│   ├── artifact_manifest.csv           # uint64 数学 reference 资产
│   └── hardware/
│       ├── abi.json
│       ├── hpu_mem_config.json
│       ├── hpu_mem_image.u32.bin
│       ├── line_map.csv
│       ├── hardware_manifest.csv
│       ├── mod_ctx_map.csv
│       ├── twiddle_map.csv
│       ├── images/**/*.u32.bin
│       └── images/**/*.u32.hex.txt
└── SHA256SUMS
```

`output/`、`outputs/` 和 `build/` 仍是生成目录，不应提交进任何源码仓库。
GitHub Actions 生成后以 artifact 形式下载。源码仓库只保存生成器、接收器、
schema 和固定版本号。

## 5. 数据接口

### 5.1 面向 HPU 的二进制

硬件数据统一使用：

```text
element       = uint32_t
byte order    = little-endian
line size     = 256 bytes
words/line    = 64
line_offset   = relative to HPU_MEM base
line_count    = ceil(payload_words / 64)
```

例如一个 RNS 分量有 4096 个 32-bit 系数：

```text
4096 * 4 bytes = 16384 bytes = 64 HPU lines
```

不足整 line 的尾部必须补零，并在 manifest 中同时记录
`payload_words` 和 `padded_words`。生产者不得依赖宿主机字节序。

顶层 `test_data/*.bin` 当前是 little-endian `uint64` 数学 reference；它不能直接
送给 HPU。真正用于 DLOAD 的文件必须来自
`test_data/hardware/**/*.u32.bin`。

### 5.2 line map

现有 `line_map.csv` 表头保持不变：

```csv
path,role,shape,address_byte,line_offset,line_count,payload_words,payload_bytes,padded_words,padded_bytes
```

消费者以 `line_offset` 和 `line_count` 为准。`address_byte` 只是生产者参考布局的
展示值，不能覆盖 Nexus-AM 选择的实际 HPU window base。

消费者必须检查：

- `line_count > 0`；
- `line_offset + line_count <= configured_window_lines`；
- 所有需要同时存在的区域不重叠；
- 每个文件大小等于 `padded_bytes`；
- shape、payload words 和文件长度相互一致；
- 指令表引用的 `data_path` 在 line map 中唯一存在。

### 5.3 人工可读展示

每个 `.u32.bin` 必须有同名 `.u32.hex.txt`。当前格式可继续使用：文件头写
`role`、logical shape、payload/padded words、line offset/count，正文每行展示
8 个固定 8 位的十六进制 `uint32`。

`DELIVERY.md` 还应自动汇总一张区域表：

| data path | role | shape | line range | payload bytes | SHA-256 |
|---|---|---|---|---:|---|
| `images/input/a.u32.bin` | input | `1x4096` | `[64,128)` | 16384 | `...` |
| `images/output/c.u32.bin` | output | `1x4096` | `[192,256)` | 16384 | `...` |

这张表只由生成器产生，不手工维护。

### 5.4 input、output 和 golden 必须分离

用于运行的输出区域必须在 Nexus-AM 中先写入非 golden 的 poison。golden 保留在
ELF 的只读区域或 HPU window 外，不能预装到 DSTORE 目标地址，否则“HPU 没有
写回”也可能错误通过比较。

`hpu_mem_image.u32.bin` 可以用于数据布局检查，但若其中已经包含期望输出，消费者
不得把它原样作为可执行 testcase 的初始 DDR 镜像。

## 6. 指令和 x10/x11 接口

### 6.1 三种文件的用途

| 文件 | 用途 | 能否直接执行 |
|---|---|---|
| `<case>.asm` | 人工审查 HPU 助记符和算法顺序 | 否；当前是 C 字符串片段 |
| `<case>.inst32` | 检查 HPU 指令的 32-bit 编码 | 否；当前是每行 32 个 ASCII bit |
| `<case>.cmd26` | 对照 RTL decode/precode | 否；不含 GPR 运行时值 |
| `program/program.S` | 普通 RV 指令 + HPU `.word` | 是；由 RISC-V 工具链汇编并链接 |

`.cmd26` 永远不是 CPU 可执行文件，也不能代替 custom1 sideband。

### 6.2 x10/x11 的准确含义

对 custom1 DLOAD/DSTORE：

```text
GPR[x10] = HPU_MEM-relative line_offset
GPR[x11] = line_count
unit      = 256 bytes/line
```

HPU 32-bit 指令字只编码“rs1 是 x10、rs2 是 x11”。真正的 offset/count 数值必须
在执行该 HPU 指令前由普通 RISC-V 指令写入寄存器。也就是说，下面只有最后一行
属于 HPU encoder：

```asm
li x10, 64              # 普通 RISC-V：输入 A 的相对 line offset
li x11, 64              # 普通 RISC-V：4096 个 u32 = 64 lines
.word 0x00b5102b        # HPU custom1: dload x10, x11, p0, poly
```

`li` 是 RISC-V assembler 伪指令，可能展开为一条或多条标准 RISC-V 指令。
inline-asm 的 HPU parser 不需要编码 `li`；它只需生成正确的 `program.S`，再由
Nexus-AM 使用的 RISC-V GNU 工具链完成汇编。

生产者已经有 `include/util/riscv_asm.hpp` 的 `li/la/mv/...` 文本生成帮助函数，
但当前 delivery flow 尚未用它生成 DMA 前的 x10/x11 装载。这是 v2 producer
必须补齐的功能。

### 6.3 `program.S` 函数 ABI

生产者为每个 case 生成一个可从 C 调用的函数：

```asm
    .section .text.hpu_program,"ax",@progbits
    .balign 4
    .globl hpu_program_rns_add
    .type hpu_program_rns_add,@function
hpu_program_rns_add:
    li x10, 0
    li x11, 1
    .word 0x00b5292b    # dload mod context -> p4

    li x10, 64
    li x11, 64
    .word 0x00b5102b    # dload input A -> p0

    li x10, 128
    li x11, 64
    .word 0x00b5122b    # dload input B -> p1

    .word 0x6000000b    # pmodld 0
    .word 0x0400400b    # padd p2, p0, p1

    li x10, 192
    li x11, 64
    .word 0x00b5542b    # dstore p2 -> output, release
    .word 0x7000000b    # terminal psync
    ret
    .size hpu_program_rns_add,.-hpu_program_rns_add
```

约束如下：

- `x10`、`x11` 和其他 caller-saved temporaries 可被函数改写；
- line offset 必须是相对量，函数中不得硬编码 HPU window 绝对地址；
- 每条 custom1 前都必须有可追溯的 rs1/rs2 值；
- DLOAD 和 DSTORE 都必须提供非零、正确的 count；
- 完整程序只在末尾发一条 terminal PSYNC；
- 函数不写 HPU window CSR、不等待中断、不做 cache 操作；这些属于 Nexus-AM；
- 函数不使用 UART，不决定 PASS/FAIL。

### 6.4 指令绑定表

`instruction_manifest.csv` 每行对应一条 HPU 指令，建议固定表头：

```csv
sequence,kind,asm,inst32_hex,cmd26_hex,rs1,rs1_value,rs2,rs2_value,data_path,line_offset,line_count,terminal
```

示例：

```csv
0,custom1,"dload x10, x11, p0, 1, 0",0x00b5102b,0x02000002,x10,64,x11,64,"images/input/a.u32.bin",64,64,false
1,custom0,"padd p2, p0, p1",0x0400400b,0x00080080,,,,,,,false
2,custom1,"dstore x10, x11, p2, 1",0x00b5542b,0x02000015,x10,192,x11,64,"images/output/c.u32.bin",192,64,false
3,custom0,"psync",0x7000000b,0x00e00000,,,,,,,true
```

`rs1_value/rs2_value` 必须与 `line_offset/line_count` 相等；`data_path` 必须引用
`line_map.csv`。即使 `program.S` 中已经有 `li`，该 CSV 仍是机器检查和人工
表格展示的唯一绑定依据。

## 7. `delivery.json`

建议的最小 schema：

```json
{
  "format_version": 2,
  "status": "EXECUTABLE",
  "case_id": "rns_add_4096_q1",
  "producer": {
    "repository": "https://github.com/cuihu2/inline-asm",
    "commit": "4399883b9e1fa249b99d48c7e919ee52acc662bc"
  },
  "parameters": {
    "N": 4096,
    "q": [65537],
    "p": [],
    "seed": 1
  },
  "data_abi": {
    "element": "uint32",
    "byte_order": "little-endian",
    "line_bytes": 256,
    "line_offset_origin": "HPU_MEM_BASE"
  },
  "program": {
    "entry_symbol": "hpu_program_rns_add",
    "source": "program/program.S",
    "instruction_manifest": "program/instruction_manifest.csv",
    "terminal_psync_count": 1
  },
  "hardware": {
    "line_map": "test_data/hardware/line_map.csv",
    "minimum_window_lines": 256
  },
  "checksums": "SHA256SUMS"
}
```

模数、shape 或 program 改变后必须重新生成整个包，不能手工只改 JSON。
不兼容 schema 变化必须增加 `format_version`。

## 8. Nexus-AM 接收流程

未来接收脚本应执行以下步骤：

1. checkout `delivery.json` 指定且 Nexus-AM 允许的 producer commit；
2. 在 producer 仓库运行 `hpu_delivery`；
3. 要求 `format_version == 2` 且 `status == EXECUTABLE`；
4. 验证 `SHA256SUMS`、全部 manifest、字节序、shape 和文件大小；
5. 验证 line map 不重叠、容量不越界，并把相对 offset 适配到本地 window；
6. 逐条用 inline-asm encoder 重新编码 `asm`，对比 `inst32_hex/cmd26_hex`；
7. 检查每条 custom1 的 data binding、x10/x11 值和 `program.S` 装载顺序；
8. 把所需 `.u32.bin` 用 `.incbin` 或等价只读 object 链接进 testcase ELF；
9. 把 `program.S` 链接进同一个 ELF；
10. 反汇编 ELF，确认普通 RISC-V GPR 装载、HPU words 和唯一末尾 PSYNC；
11. 运行 testcase：准备 DDR、配置 CSR、调用 program、同步、invalidate、逐项比较；
12. GitHub Actions 上传 ELF/BIN/TXT、producer identity、manifest 和校验报告。

生成物只能放入 GitHub Actions artifact，不提交进 Nexus-AM Git 历史。

## 9. 必须拒绝的交付

出现以下任一情况，消费者必须停止构建：

- producer commit 不匹配或工作树生成来源不明；
- 缺少 `delivery.json`、`SHA256SUMS`、line map 或 instruction manifest；
- `status` 不是 `EXECUTABLE`；
- 可执行流中任何 DLOAD/DSTORE 仍使用未绑定的 `x0,x0`；
- `line_count == 0`，地址越界或数据区域重叠；
- `program.S` 的 x10/x11 数值与 CSV/line map 不一致；
- `.asm`、`.inst32`、`.cmd26` 和 encoder 重新编码结果不一致；
- HPU window 所需 lines 超出 testcase 配置；
- 模数、N、shape 或 golden 与 testcase 声明不一致；
- output 初始内容就是 golden；
- 完整程序没有 terminal PSYNC，或存在多个/中途 PSYNC；
- 只给 `.cmd26`、`.inst32` 或 C 字符串 ASM，却声称可以直接执行。

## 10. 当前迁移计划

建议按以下顺序实现，而不是一次接入完整 FHE 算子：

1. inline-asm 先生成一个 `N=4096, Q=1` 的 RNS PADD smoke v2 包；
2. 包含两个 4096×`uint32` 输入、一个 poison 输出、一个独立 golden 和一条
   mod-context；
3. 生成可执行 `program.S`：mod DLOAD、A/B DLOAD、PMODLD、PADD、DSTORE、
   terminal PSYNC；
4. Nexus-AM 用该包替换当前本地 `.rept` fixture 和手写 instruction words；
5. 双方 CI 同时校验数据表、instruction manifest、ELF 反汇编和 self-check；
6. 小包闭环后，再逐步接 NTT、CMULT、KeySwitch 等更大的 package。

在上述 v2 包落地前，当前 inline-asm 输出仍应标记为
`PENDING_RELOCATION`，Nexus-AM 当前 smoke 数据仍应标记为本地 bootstrap
fixture。两者都不能被描述成“inline-asm 已经把完整可执行用例传给 Nexus-AM”。
