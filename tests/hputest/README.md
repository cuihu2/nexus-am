# HPU tests for Nexus-AM

`hputest` is the canonical Nexus-AM source tree for HPU bring-up and IT
testcases.  It contains the eleven bring-up/return probes plus the 49 migrated
IT cases.  Generated ELF, BIN, TXT, object files, and archives are never
committed.  GitHub Actions builds them as short-lived downloadable artifacts.

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

GNU as does not natively recognize HPU mnemonics.  The
`third_party/inline-asm` git submodule therefore pins
[`cuihu2/inline-asm`](https://github.com/cuihu2/inline-asm) commit
`b405f2ad7b0901930d81edb79a5167ab028c4dbd` from its
[`main` branch](https://github.com/cuihu2/inline-asm/tree/main).
This upstream commit includes the STG and DMA encoding fixes. AM no longer
uses the `HPU_SEAL_manual_0905` trial branch; this switch does not modify
that historical branch or `HPU_SEAL`.
The branch recorded in `.gitmodules` identifies the upstream source; normal
builds and CI use the committed gitlink, not the latest remote branch head.
Before
any testcase is built,
Nexus-AM runs the producer generation stages needed for its MM delivery and
validates the selected MM parameters, data geometry, existing producer
manifest values, four DMA relocations, ten instruction words, and all 4096
PMUL golden coefficients.

The three producer tools run under
`OUTPUT_ROOT/inline-asm-producer/<producer_commit>/`, leaving the submodule
source tree unchanged. The selected `outputs/mm` delivery is then checked
and copied into `HPU_GENERATED_ROOT/inline-asm/mm`; no testcase consumes
unvalidated binaries left in the submodule's `outputs/` directory.

The build then consumes the producer output in two concrete ways:

1. `.incbin` links `input_a`, `input_b`, `expected`, and `mod_ctx` directly
   from the validated `HPU_GENERATED_ROOT/inline-asm/mm/test_data/hardware`
   directory; Nexus-AM no longer synthesizes these arrays with `.rept`.
2. Cases 08 and 09 link the validated, opcode-mapped `mm.c` and call
   `hpu_program_mm()`. AM maps only the low seven opcode bits of the
   producer's HPU custom0 words from `0x0B` to `0x5B`; all ten payloads,
   four relocations, fixed `x10`/`x11` assignments, and software
   object-length checks remain intact.
   The producer emits exactly one PSYNC at the end of the complete program;
   AM does not split it into phases or insert a modulus-load barrier.

The programming manual at the pinned commit, sections 6.1 and 6.3, supersedes
the earlier PDF's intermediate-barrier requirement. Hardware maintains the
dependencies between modulus DLOAD, PMODLD, computation, and object reuse.
Case 08 consumes the final notification through PLIC; case 09 polls and
clears the final MMIO event. Other arithmetic and object-lifecycle cases
also omit internal PSYNC barriers and retain their terminal completion wait.
`completion_wait()` and `completion_clear()` only wait/acknowledge; they
never issue an HPU instruction. See
[manual update notes](docs/MANUAL_04_TESTCASE_UPDATE.md) for the selected
STG/DMA encodings and current object-length/lifecycle constraints.

This branch switch consumes the existing fixed MM path (`N=4096`, one RNS
component, `q=50061313`). It does not enable a complete SEAL/CKKS
application flow. STG words follow the pinned manual section 3.2:
`pdata` occupies both bits [27:25] and [24:22], while `ptwid` occupies [16:14].
The stale example words are superseded by that field formula. Independent
word/precode checks prevent falling back to the old layout. DMA words now
place the object in [27:25], `rs2` in [24:20], `rs1` in [19:15], operation
in [14:13], direction in bit 12, and the small-bank flag in bit 7.
DSTORE operation is `rel << 1`.
For both custom opcodes, cmd26 retains the complete `inst32 >> 7`; custom1
additionally sets bit 25. The runtime `x10=line offset`, `x11=line count`
ABI is unchanged. DSTORE hardware uses `OBJ.len`, not `x11`, for its actual
length; the producer still loads `x11` and checks that the supplied span
count equals its software-tracked object length. Both DSTORE `rel` values
release the source object, so a successful store must not be followed by
another PFREE for that same allocation. The previously
blocked transform/application cases stay blocked until their own complete
program/data/golden contracts are validated; fixing encoding alone does not
qualify a functional testcase.

IT has reported failures in cases 06, 07, and 08. They need new ELF/BIN
files built with this pinned producer and another IT run; switching the
encoder does not establish the cause of those failures or make them PASS.

The simpler cases still use one-operation C adapters, but their named words
are generated at build time by the same real encoder and receive the same
low-seven-bit opcode mapping. The tracked
`include/hpu/encoding.h` contains no copied instruction values.

### HPU 主 opcode：0x0B → 0x5B

上游固定提交仍生成 `0x0B`。本次在 AM 构建接收层统一映射，**不修改
inline-asm/main、gitlink 或 RTL**。物理 RISC-V 主 opcode 改用 custom2
`0x5B`，HPU 内部仍为 `cmd_kind=0`；文档和用例 ID 中的旧称 custom0/C0
指内部命令类别，不表示新 ELF 仍使用 `0x0B`。

```text
new_inst = (old_inst & 0xFFFFFF80) | 0x5B
```

仅对生产者 opcode 为 `0x0B` 的 HPU 指令应用上式。`inst[31:7]`、cmd26、
寄存器绑定、数据和指令顺序不变；DLOAD/DSTORE 的 custom1 `0x2B` 完全不变。
例如 PSYNC 为 `0x7000005B`、PMODLD 0 为 `0x6000005B`、PFREE p0 为
`0x8000005B`。flag 位仍在 bit 7，flag=1 的机器码低字节会是 `0xDB`，
不能用“所有指令都以 5B 结尾”来检查。

产物 `provenance/inline-asm-mm/` 同时保留目标 `mm.c`、`mm.inst32`、
`mm.cmd26`、`encoder_words.tsv` 和 `upstream/` 下的原始同名文件，`opcode_map.csv` 按 MM
指令记录映射前后的 word，便于排查 ELF 与 simv 版本是否配套。
**运行新 ELF 的 CPU 前端必须已将 `0x5B` 识别为 HPU 并映射到
`cmd_kind=0`**；仅更新测试文件不能让旧 simv 自动兼容。本次不启用被阻塞的
功能用例，也不代表已通过 VCS。

The complete producer/consumer contract and the exact current MM file mapping
are documented in
[docs/INLINE_ASM_DELIVERY_INTERFACE.md](docs/INLINE_ASM_DELIVERY_INTERFACE.md).

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
case makes the build fail.  Build artifacts are partitioned by what an IT user
is likely to run together:

The mapping between Gantt-chart labels such as configuration, instruction,
operator, performance, and application tests and this source hierarchy is
fixed in
[docs/TESTPOINT_SCHEDULE_ALIGNMENT.md](docs/TESTPOINT_SCHEDULE_ALIGNMENT.md).
Planning rows must carry the exact `case_id` from `cases.tsv`; a parallel set
of informal testcase numbers is not authoritative.

Every migrated ELF links the producer's two immutable one-RNS fixtures
`RNS_A` and `RNS_B` (4096 little-endian 32-bit
coefficients, 16 KiB each).  It does not link the smoke-only MM golden,
modulus record, or generated `mm.c` program unless its testcase explicitly
uses that program.

| Group | Contents |
|---|---|
| `core` | Bring-up, return probes, configuration, data paths, basic/control instructions, and CPU/HPU structural cases |
| `transform` | PNTT, PINTT, BConv, NTT/INTT sequences, and their performance cases |
| `fhe` | KeySwitch, ciphertext multiplication, relinearization, other algorithm cases, and the application case |

Twenty-five migrated sources issue real CSR/HPU operations and contain a
software self-check.  The remaining twenty-four sources are deliberately
fail-closed: the producer has not yet supplied a receiver-ready combination
of complete N=4096 data, immutable golden results, and a resolved DMA-span
table for those algorithms/performance measurements.  The blocked set is
`INS_C0_005/006`,
`CMB_001/002/003/004/005/010/011/012/013/014/015`,
`STING_CMB_007`, `STR_003/004`, `STING_STR_005`,
`PERF_001/002/003/004/005/006`, and `APP_001`.  In particular, the transform
cases stay blocked until the producer supplies the real N=4096 data,
pre/post-twist and stage-twiddle layout; a one-line stand-in is not accepted.

The twenty-four blocked sources are still cross-built so the source/API boundary
cannot rot, but they do not issue invented HPU commands and deliberately
return 1.  Both
`CASE_MANIFEST.tsv` and `NOT_QUALIFIED.tsv` mark them
`blocked-not-issued`; they are not qualification results.

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

On pushes to `master` and manual runs, GitHub Actions builds and uploads all
three groups separately as `nexus-am-hpu-core-workloads`,
`nexus-am-hpu-transform-workloads`, and `nexus-am-hpu-fhe-workloads`.  Pull
requests build the `core` group as the fast structural gate.  Artifacts are
retained for seven days; none of them are committed to Git.

## PASS/FAIL boundary

For finite cases, `main() == 0` reaches the Nexus-AM good trap; a nonzero
return reaches the bad/fail trap.  Case 01 intentionally never returns and
must be stopped with an IT simulation cycle limit while inspecting waves.

A green GitHub Actions job proves cross-compilation and the scripted structural
and semantic artifact checks.
It does **not** claim RTL/VCS execution passed.  RTL failures remain external
IT evidence and are not repaired by this source repository.
