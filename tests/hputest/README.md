# HPU tests for Nexus-AM

`hputest` is the canonical Nexus-AM source tree for HPU bring-up and IT
testcases.  It contains the eleven bring-up/return probes plus the 49 migrated
IT cases.  Generated ELF, BIN, TXT, object files, and archives are never
committed.  GitHub Actions builds them as short-lived downloadable artifacts.

本轮按 v2 测试点更新后的实际覆盖、各指令轮次和未完成项见
[V2_COVERAGE.md](docs/V2_COVERAGE.md)。后续49项中29项有软件自检，20项尚未接入；
软件自检不等于整个测试点或IT验证已通过。00冒烟源码保持不变。

03/04运行过久或结果失败时，先看 [运行时间与UART诊断](docs/RUNTIME_UART_DIAGNOSTICS.md)：
默认摘要包、单子项选择及独立全量结果包均已支持；不会以误差容限放宽模整数自检。

## Source layout

The bring-up suites are deliberately flat at the testcase level:

```text
src/00_bringup/
├── 001_hpu_smoke/
│   ├── 01_dload_hold.c
│   ├── 02_read_write_mmio_csr.c
│   ├── 03_dload_poll_mmio.c
│   ├── 04_psync_irq.c
│   ├── 05_dload_psync_irq.c
│   ├── 06_dload_dstore_poll_mmio.c
│   ├── 07_dload_dstore_psync_irq.c
│   ├── 08_dload_compute_dstore_psync_irq.c
│   └── 09_dload_compute_dstore_poll_mmio.c
└── 002_main_return_probe/
    ├── 01_return_0.c
    └── 02_return_1.c

src/common/
├── hpu_rns_fixture.S          # inline-asm MM input A/B via .incbin
├── hpu_mm_fixture.S           # MM golden/mod_ctx; cases 08/09 only
└── hpu_irq.c                  # cases 04/05/07/08 的 PLIC 公共机械层

src/01_configuration/          # 7 migrated IT cases
src/02_data_paths/             # 6 migrated IT cases
src/03_compute_instructions/   # 9 migrated IT cases
src/04_composite_instruction_sequences/ # 13 migrated IT cases
src/05_cpu_hpu_structural_connectivity/  # 7 migrated IT cases
src/06_performance/            # 6 migrated IT cases
src/07_full_application/       # 1 migrated IT case

include/hpu/steps.h            # short, one-step public testcase API
include/hpu/result.h           # UART START/PASS/FAIL reporting helpers
runtime/                       # mechanical helpers, never whole scenarios
├── it_core.c                  # data/cache/CSR waits/one-instruction issue
└── it_compute.c               # coefficient-wise C reference comparisons

third_party/
└── inline-asm/                 # pinned main branch, including STG/DMA fixes

build/                          # ignored: generated outputs only
├── inline-asm-producer/<producer_commit>/ # isolated producer working directory
└── generated/                  # validated, selected MM delivery
    ├── include/hpu/inline_asm_mm_delivery.h
    └── inline-asm/mm/
```

Each source has its own readable `main()`.  The MMIO register setup, data preparation,
individual HPU commands, synchronization, result comparison, and final
`return 0`/`return 1` are visible in that file; there is no testcase-kind enum,
central dispatcher, one-call scenario wrapper, or 4500-line scenario file.
Only mechanical one-operation adapters and reference helpers are split by
hardware concern under `runtime/`.  The shared HPU-IT context is already clear
from the directory, so public step names do not repeat an `hpu_it_` prefix.
HPU register writes and reads stay at MMIO-address granularity, for example
`csr_write(CSR_BASE_LO, value)`, instead of being hidden in a combined
initialization function.  Instructions remain equally visible as
`dload(P0, LINE_A, POLY_LINES)`, `padd()`, `dstore_release(...)`, and
`psync()`.

The IT environment exposes UART.  Every testcase prints a stable `START`
record and then `PASS`, `FAIL`, `NOT_QUALIFIED`, or `HOLD` as appropriate;
ordinary failures include the source line.  UART is diagnostic evidence, while
the authoritative result remains `main()==0` for a completed self-check and
`main()!=0` for failure.  The DLOAD-hold case intentionally does not return
and must be stopped by the simulation cycle limit.

## Smoke suite 001

| Source | Operation | Completion |
|---|---|---|
| `01_dload_hold.c` | DLOAD then `while (1)` | Intentional waveform hold |
| `02_read_write_mmio_csr.c` | Program/read the HPU MMIO register window | Return 0/1 |
| `03_dload_poll_mmio.c` | 64-line DLOAD and poll MMIO STATUS busy | Return 0/1 |
| `04_psync_irq.c` | Idle PSYNC; PLIC handler handles the completion IRQ | Return 0/1 |
| `05_dload_psync_irq.c` | DLOAD + PSYNC; PLIC handler sets `volatile sync_flag` | Return 0/1 |
| `06_dload_dstore_poll_mmio.c` | DLOAD → poll STATUS busy 1→0 → DSTORE → poll STATUS busy 1→0; compare 4096 coefficients; no PSYNC | Return 0/1 |
| `07_dload_dstore_psync_irq.c` | DLOAD + DSTORE + PSYNC; wait by PLIC interrupt; compare 4096 coefficients | Return 0/1 |
| `08_dload_compute_dstore_psync_irq.c` | Producer MM program; PSYNC interrupt; PMUL/golden/C check | Return 0/1 |
| `09_dload_compute_dstore_poll_mmio.c` | Same producer MM program; poll MMIO completion; PMUL/golden/C check | Return 0/1 |

Cases 04 and 05 both test interrupt completion, but they are not duplicates:
case 04 submits only an idle PSYNC and isolates the interrupt path; case 05
places a real 64-line DLOAD before PSYNC. Cases 06 and 07 check the same
loopback data, but deliberately use different command sequences. Case 06
finishes DLOAD through MMIO STATUS polling before submitting DSTORE, then
polls DSTORE separately. It issues no PSYNC, never accesses `CSR_IRQ`, and
does not call the PLIC interrupt path. Case 07 submits DLOAD and DSTORE
without a software wait between them, then uses PSYNC and PLIC source 257
for completion. A `csrr`-based
HPU-status case is deliberately absent because the current hardware defines
no architectural HPU CSR number, privilege level, or idle encoding.

06 的每个阶段都单独观察 `BUSY=1` 后再等待 `BUSY=0`，并重新清零
`saw_busy`。这样既不会把命令尚未启动时的初始空闲当作完成，也不会把
DLOAD 与 DSTORE 之间的 DMA 空闲间隔当作 DSTORE 完成。若 CPU 未观察到
某阶段的忙状态，用例会保守地超时报失败；这表示未取得完成证据，不能单凭
该结果认定 RTL 有错。07/08 使用程序末尾的 PSYNC 中断同步，09 轮询末尾
PSYNC 的 MMIO 完成电平；08/09 的模表 DLOAD 与 PMODLD 之间不再插入 PSYNC。

For `LINKNAN_HPU_IT` builds, the `riscv64-xs` AM startup uses the LinkNan PLIC
window at `0x04000000`.  Before entering S-mode it establishes an
OpenSBI-style quiescent PLIC state: priorities for sources 1 through 257 are
zeroed, all nine enable words for M/S contexts 0 and 1 are cleared, and both
thresholds are set to 7.  `irq_open()` then configures only HPU source 257
(priority index 256, S-context threshold 0, enable source 257) before enabling
SEIP.  It also makes CTE skip both CLINT timer initialization and timer enable,
so cases 04, 05, 07, and 08 cannot accept a timer interrupt as an HPU
completion.  This initialization order follows
the generic sequence in
[`OpenSBI plic.c`](https://github.com/riscv-software-src/opensbi/blob/35511bc6ee1c9c17b6a89b44c52e2044bb51b979/lib/utils/irqchip/plic.c),
adapted to Nexus-AM's zero-based priority-index helper.
`irq_open()` then directly reads back LinkNan priority `0x04000404`, S-context
enable word 8 at `0x040020a0`, and threshold `0x04201000`; a map or setup
mismatch therefore fails before `irq_wait()`.  The handler claims and completes
source 257 at `0x04201004`.

Every `001_hpu_smoke` ELF embeds the two one-modulus RNS inputs generated by
inline-asm's `outputs/mm` delivery.  Each input contains 4096 little-endian
`uint32_t` coefficients (16 KiB, 64 HPU lines).  Cases 08 and 09 additionally
embed
the producer's immutable MM golden and one-line modulus context.  The two
minimal return probes remain fixture-free so they continue to isolate the
Nexus-AM termination path.

The 256-line HPU window is intentionally easy to inspect in a waveform:

```text
line 0       RNS input A (64 lines, 4096 coefficients)
line 64      RNS input B (64 lines, 4096 coefficients)
line 128     output (64 lines, 4096 coefficients)
line 192     modulus record (1 line)
line 256     configured window end (lines 193..255 remain unused)
```

Cases 06, 07, 08, and 09 initialize the whole output region with poison before
issuing HPU commands. Case 06 waits for DSTORE's observed BUSY 1→0 transition;
cases 07, 08, and 09 wait for their terminal PSYNC completion. Only then do
they invalidate 16 KiB and compare all 4096 coefficients against immutable
ELF data. Cases 08 and 09 also
recompute every
pointwise product as `(uint64_t)A[i] * B[i] % 50061313` in C, so both the
producer golden and the HPU result must agree with an independent oracle.
Any mismatch or other detected failure returns 1 from `main()`; a completed
self-check returns 0.  UART records expose that decision but do not replace it.

### 06/07/08 失败日志定位

失败输出保留文件名和行号，并补充触发条件：配置寄存器的 actual/expected、
初始化/DLOAD/DSTORE/计算/中断等待阶段，以及实际采样的 STATUS/FAULT/IRQ。
06 的超时区分 `busy_not_observed`（未观察到忙）和 `busy_not_cleared`（忙不退出）。
数据自检只报告首个错误系数的索引、实际值和期望值，并区分 golden/C 与 HPU/C
不一致。中断处理程序只缓存诊断信息，由主程序在失败后打印，不重复 claim PLIC。
这些日志不放宽任何判据，也不在正常轮询的每一轮或中断处理函数中打印。

## HPU instruction source

`third_party/inline-asm` 固定引用 [cuihu2/inline-asm main](https://github.com/cuihu2/inline-asm/tree/main)
的提交 `69030963e71dbcf32897e8ae08695cfa2e65d79a`。正常构建使用固定gitlink，
不是每次自动取远端HEAD；本次未修改上游源码或RTL。
同步差异和新旧包的使用边界见 [main更新说明](docs/INLINE_MAIN_6903096.md)。

上游已原生生成计算/控制custom2 `0x5B` 与DMA custom1 `0x2B`，
AM只校验并原样接收，不再执行 `0x0B→0x5B` 转换。旧HPU `0x0B` 文件直接拒绝。
`opcode_map.csv` 保留原名称作为追溯表，本版source/target相同，payload和cmd26不变。
`include/hpu/encoding.h` 不含手写机器码；所有单指令适配由同一个真实producer编码器生成。
bit7仍是flag，flag=1时低字节可能是 `0xDB`，不能只检查字符串是否以5B结尾。

新STG显式使用三个对象：

```text
pntt/pintt pdst, psrc, ptwiddle, stage, mode, flag
word = (OPC << 28) | (pdst << 25) | (psrc << 22)
     | (ptwiddle << 14) | (stage << 10) | (mode << 8) | (flag << 7) | 0x5B
```

03单stage使用不同的目的/源/twiddle槽；04整体NTT/INTT由producer在p0/p3之间交替写入并释放旧源。
不再使用旧两对象语法或隐式原地覆盖。DMA仍为标准GPR字段：
`obj[27:25], rs2[24:20], rs1[19:15], op[14:13], dir[12], flag[7]`；
DSTORE的op为`rel<<1`，实际传输长度为`OBJ.len`，运行时x10/x11仍传line offset/count。
任何rel值的DSTORE完成后均释放对象，不能再次PFREE同一份分配。

数据域也必须区分：

- host uint64数学数据保持自然序。
- 系数域硬件数组（包括BConv）使用 `physical[p]=logical[bit_reverse(p)]`。
- NTT域硬件数组（包括MM）使用 `physical[p]=logical_ntt[forward_layout[p]]`。
- PNTT按照loader batch/lane执行蝶形后P；PINTT按逆向stage执行P⁻¹后蝶形，消费lazy-scale twiddle。
- pre/post因子也按物理序接收，不能混用旧natural-order/group-major表。

producer生成在 `OUTPUT_ROOT/inline-asm-producer/<commit>/`，不会读写submodule内的旧outputs。
MM、单stage、完整NTT/INTT和BConv分别导入本次generated目录。
构建前运行producer编码器和NTT硬件模型回归，再独立检查布局、实际程序、DMA绑定与数学golden。
上游冻结RTL dump回归不是当前IT/VCS执行结果。

冒烟08/09直接调用原生 `hpu_program_mm()`，仍为10条指令、4次DMA和末尾一次PSYNC。
其余算子也只在完整程序末尾PSYNC；06仍是无PSYNC的纯MMIO DMA测试。
UART摘要/全量诊断、stage cycle、单子项选取与所有精确比较条件保留。
生成C、ASM、inst32、cmd26、原始数学数据和DMA/物理布局表随artifact的provenance保留；
生成数据重新排序后，旧ELF/BIN不能作为本版交付替用。

完整接收接口见 [INLINE_ASM_DELIVERY_INTERFACE.md](docs/INLINE_ASM_DELIVERY_INTERFACE.md)。

## What the MMIO register checks prove

The smoke sequence deliberately checks configuration in layers:

1. Write and read back `BASE_LO/HI` and `SIZE_LO/HI`.  This proves that CPU
   MMIO writes reached the shadow CSRs.
2. Write `COMMIT`, then poll `STATUS.window_valid`.  This proves that the
   nonzero window configuration was accepted.  `COMMIT` itself is a pulse and
   is not checked by reading back a value of 1.
3. Require `STATUS.busy=0`, `STATUS.fault_valid=0`, FAULT clear, and IRQ clear
   before issuing work.
4. Case 03 observes DLOAD `busy` go high and then low.  Case 04 isolates the
   idle-PSYNC interrupt path, while case 05 proves DLOAD followed by PSYNC can
   reach the same interrupt handler.
5. Case 06 closes the DMA path by polling each DMA transfer's BUSY 1→0
   transition separately, without PSYNC or IRQ-register accesses. Case 07
   submits DLOAD + DSTORE + PSYNC and waits for a CPU interrupt. Both compare
   all 4096 loopback coefficients. Cases 08
   and 09 add the same PMUL program and three-way comparison; case 08 waits by
   interrupt, while case 09 polls the MMIO completion level.

There is no separate `HPU initialization done` CSR.  `window_valid` is a
configuration-level indication; the data-closing cases provide the stronger
functional result.

## Main return probe 002

These two minimal programs contain no HPU instruction or self-check operation.
They print their expected return path, then isolate how the Nexus-AM termination
path and the IT simulator report the value returned by `main()`.

| Source | `main()` result | Source-level expectation |
|---|---:|---|
| `01_return_0.c` | 0 | Nexus-AM good trap; VCS PASS candidate |
| `02_return_1.c` | 1 | Nexus-AM bad trap; VCS FAIL candidate |

The table states the expected software path, not a claimed VCS result.  Run
both ELF files in the same IT/VCS environment and retain both simulator logs
to confirm the exact PASS/FAIL text and exit status.

Common addresses are visible in `include/hpu/layout.h` and `include/hpu/csr.h`:
CSR base `0x08000000`, HPU memory base `0x87000000`, and a 256-line bring-up
window.  DLOAD/DSTORE explicitly use `x10=line offset` and `x11=line count`.

## Migrated IT suites and artifact groups

The 49 migrated sources retain the original hierarchy and testcase IDs from
`IT-SCPU-TestCases`.  Files below `src/common/` and `runtime/` are deliberately
excluded from testcase discovery.  The tracked `cases.tsv` file is the
canonical roster: deleting, renaming, duplicating, or silently reclassifying a
case makes the build fail. Internal build groups remain available for local
selection, while the downloadable package merges them into source chapters:

The mapping between Gantt-chart labels such as configuration, instruction,
operator, performance, and application tests and this source hierarchy is
fixed in
[docs/TESTPOINT_SCHEDULE_ALIGNMENT.md](docs/TESTPOINT_SCHEDULE_ALIGNMENT.md).
Planning rows must carry the exact `case_id` from `cases.tsv`; a parallel set
of informal testcase numbers is not authoritative.

基础用例使用producer的两组不可变一RNS输入 `RNS_A/RNS_B`，各4096个32位系数。
整体NTT/INTT/BConv改用各自的完整数据、常量和golden交付，不用MM输入冒充算子数据。
未接入用例不再为了凑数据而强制保留无关fixture。

| Group | Contents |
|---|---|
| `core` | Bring-up, return probes, configuration, data paths, basic/control instructions, and CPU/HPU structural cases |
| `transform` | PNTT, PINTT, BConv, NTT/INTT sequences, and their performance cases |
| `fhe` | KeySwitch, ciphertext multiplication, relinearization, other algorithm cases, and the application case |

29个后续源码具有真实软件自检。03的PNTT/PINTT已补齐；04的BConv Q→P、整体NTT/INTT
已接入完整producer序列和golden，但仅覆盖各自固定基础组合。
其它20项仍未接入，原因逐项记录于 [blocked.tsv](blocked.tsv)，其中既有缺外部接口，
也有AM接收工作未实现，不能笼统归因于上游“没有数据”。原HADD的单条PADD替身已取消，
真实PADD覆盖保留在03-001；HADD需真实算法库API后再启用。

未接入源码仍交叉编译以检查接口，执行只报具体原因并return 1。
`CASE_MANIFEST.tsv`/`NOT_QUALIFIED.tsv`标记它们，但下载包不再包含这些占位ELF/BIN/TXT。

`cases.tsv` uses five explicit qualifiers: `software-self-check`,
`blocked-not-issued`, `waveform-hold`, `termination-probe-pass`, and
`termination-probe-fail`.  A software `return 0` proves only the checks written
in that C file.  Requirements such as IT monitor observations, backpressure,
STING entry, function coverage, and performance baselines remain external IT
evidence and are not promoted to PASS by the GitHub build.

## Build

Initialize the pinned producer source after cloning Nexus-AM:

```bash
git submodule update --init --recursive tests/hputest/third_party/inline-asm
```

Then build normally.  `make` generates and imports the selected MM delivery,
generates the one-operation encoding header, and only then builds a testcase:

```bash
export AM_HOME=/path/to/nexus-am
make -C tests/hputest \
  ARCH=riscv64-xs \
  CROSS_COMPILE=riscv64-linux-gnu-
```

Build one case:

```bash
make -C tests/hputest one \
  CASE=00_bringup/001_hpu_smoke/06_dload_dstore_poll_mmio
```

Build only one artifact group:

```bash
make -C tests/hputest group GROUP=core
make -C tests/hputest group GROUP=transform
make -C tests/hputest group GROUP=fhe
```

Local output is ignored under `tests/hputest/build/`.  A full build produces
60 ELF/BIN/TXT sets partitioned below `artifact/core/`,
`artifact/transform/`, and `artifact/fhe/`.  It also produces
`MANIFEST.txt`, `CASE_MANIFEST.tsv`, `NOT_QUALIFIED.tsv`, and a compact
`provenance/inline-asm-mm/` directory containing the selected producer
program, data, tables, producer commit, and resolved DMA spans.

本地生成章节下载包：

```bash
python3 tests/hputest/scripts/package-chapters.py tests/hputest/build/artifact --all
```

GitHub Actions在push/PR/手动运行中全量构建，然后统一上传
`nexus-am-hpu-workloads`，内容来自 `build/release/`。
包内按00至07章节组织，03的全部九个用例放在一起；`INDEX.tsv`列实际下载路径、用途和未就绪原因。
当前发布40组非占位产物（含冒烟与返回值探针），20项只保留索引；原始构建清单和provenance各保留一份。
产物保留7天，不提交二进制到Git。

## PASS/FAIL boundary

For finite cases, `main() == 0` reaches the Nexus-AM good trap; a nonzero
return reaches the bad/fail trap.  Case 01 intentionally never returns and
must be stopped with an IT simulation cycle limit while inspecting waves.

A green GitHub Actions job proves cross-compilation and the scripted structural
and semantic artifact checks.
It does **not** claim RTL/VCS execution passed.  RTL failures remain external
IT evidence and are not repaired by this source repository.
