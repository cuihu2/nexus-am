# HPU tests for Nexus-AM

`hputest` is the canonical Nexus-AM source tree for HPU bring-up and IT
testcases.  Generated ELF, BIN, TXT, object files, and archives are never
committed.  GitHub Actions builds them and exposes one downloadable artifact.

## Source layout

The bring-up suites are deliberately flat at the testcase level:

```text
src/00_bringup/
├── 001_hpu_smoke/
│   ├── 01_dload_hold.c
│   ├── 02_read_csr_status.c
│   ├── 03_dload_poll_csr.c
│   ├── 04_dload_psync_poll_mmio.c
│   ├── 05_dload_psync_irq.c
│   ├── 06_dload_dstore_psync.c
│   └── 07_dload_compute_dstore_psync.c
└── 002_main_return_probe/
    ├── 01_return_0.c
    └── 02_return_1.c

src/common/
└── hpu_rns_fixture.S
```

Each source has its own `main()` and shows the complete testcase sequence.
CSR writes and reads are deliberately explicit address-level calls such as
`hpu_csr_write32(HPU_CSR_BASE_LO_ADDR, value)`; they are not hidden behind a
combined initialization function.  Headers under `include/hpu/` contain only
one-operation hardware adapters and data helpers.  There is no testcase-kind
dispatcher or centralized scenario implementation.

The IT environment has no UART, so the testcases do not use `printf()` as a
result channel.  A detected error returns 1 from `main()` and a completed
self-check returns 0.  Comments next to each operation describe the waveform
signal or memory condition being tested.  The DLOAD-hold case intentionally
does not return and must be stopped by the simulation cycle limit.

## Smoke suite 001

| Source | Operation | Completion |
|---|---|---|
| `01_dload_hold.c` | DLOAD then `while (1)` | Intentional waveform hold |
| `02_read_csr_status.c` | Program/read HPU CSR state | Return 0/1 |
| `03_dload_poll_csr.c` | 64-line DLOAD and poll STATUS busy | Return 0/1 |
| `04_dload_psync_poll_mmio.c` | DLOAD + PSYNC; poll MMIO STATUS/IRQ | Return 0/1 |
| `05_dload_psync_irq.c` | DLOAD + PSYNC; PLIC handler sets `volatile sync_flag` | Return 0/1 |
| `06_dload_dstore_psync.c` | 4096-coefficient DMA loopback self-check | Return 0/1 |
| `07_dload_compute_dstore_psync.c` | 4096-coefficient PADD/C golden check | Return 0/1 |

Cases 04 and 05 intentionally submit identical HPU work.  Only their CPU
completion mechanism differs: case 04 keeps interrupts disabled and polls the
MMIO register file; case 05 waits for PLIC source 257 and then verifies MMIO
STATUS is idle.  A third `csrr`-based HPU-status case is deliberately absent:
the current hardware documentation defines no architectural HPU CSR number,
privilege level, or idle bit, so the testcase must not invent one.

Every `001_hpu_smoke` ELF embeds two deterministic one-modulus RNS
polynomials.  Each polynomial contains 4096 little-endian `uint32_t`
coefficients (16 KiB, 64 HPU lines).  The two minimal return probes remain
fixture-free so they continue to isolate the Nexus-AM termination path.

The 256-line HPU window is intentionally easy to inspect in a waveform:

```text
line 0       modulus record (1 line)
line 64      RNS input A (64 lines, 4096 coefficients)
line 128     RNS input B (64 lines, 4096 coefficients)
line 192     output (64 lines, 4096 coefficients)
line 256     exclusive window end
```

Cases 06 and 07 initialize the whole output region with poison before issuing
HPU commands.  After terminal PSYNC they invalidate 16 KiB and compare all
4096 coefficients against immutable ELF data or a C modular-add oracle.
Any mismatch or other detected failure returns 1 from `main()`; a completed
self-check returns 0.  No UART text is required for this decision.

## HPU instruction source

GNU as does not natively recognize the HPU mnemonics.  The small adapters in
`dma.h`, `arithmetic.h`, and `sync.h` therefore emit `.word`, using named words
from `include/hpu/encoding.h`.  Those words are not guessed: they are pinned to
[`cuihu2/inline-asm`](https://github.com/cuihu2/inline-asm) commit
`4399883b9e1fa249b99d48c7e919ee52acc662bc`.  The GitHub Actions HPU job checks
out that exact commit, compiles its real encoder, and verifies every mnemonic
and instruction word before building the ELF/BIN artifacts.

## What the CSR checks prove

The smoke sequence deliberately checks configuration in layers:

1. Write and read back `BASE_LO/HI` and `SIZE_LO/HI`.  This proves that CPU
   MMIO writes reached the shadow CSRs.
2. Write `COMMIT`, then poll `STATUS.window_valid`.  This proves that the
   nonzero window configuration was accepted.  `COMMIT` itself is a pulse and
   is not checked by reading back a value of 1.
3. Require `STATUS.busy=0`, `STATUS.fault_valid=0`, FAULT clear, and IRQ clear
   before issuing work.
4. Case 03 observes DLOAD `busy` go high and then low.  Cases 04 and 05 both
   issue DLOAD + PSYNC: case 04 polls MMIO, while case 05 uses the interrupt
   handler and a `volatile` flag.  These prove more than CSR readback, but not
   payload accuracy.
5. Cases 06 and 07 close the datapath with DSTORE and a 4096-coefficient
   self-check.  This is the final smoke indication that the active window and
   HPU data path worked together.

There is no separate `HPU initialization done` CSR.  `window_valid` is a
configuration-level indication; the data-closing cases provide the stronger
functional result.

## Main return probe 002

These two minimal programs contain no HPU instruction, UART output, or
self-check helper.  They isolate how the Nexus-AM termination path and the IT
simulator report the value returned by `main()`.

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

## Build

```bash
export AM_HOME=/path/to/nexus-am
make -C tests/hputest \
  ARCH=riscv64-xs \
  CROSS_COMPILE=riscv64-linux-gnu-
```

Build one case:

```bash
make -C tests/hputest one \
  CASE=00_bringup/001_hpu_smoke/06_dload_dstore_psync
```

Local output is ignored under `tests/hputest/build/`.  A full build produces
nine ELF/BIN/TXT sets plus `MANIFEST.txt` and `SHA256SUMS`.

## PASS/FAIL boundary

For finite cases, `main() == 0` reaches the Nexus-AM good trap; a nonzero
return reaches the bad/fail trap.  Case 01 intentionally never returns and
must be stopped with an IT simulation cycle limit while inspecting waves.

A green GitHub Actions job proves cross-compilation and artifact integrity.
It does **not** claim RTL/VCS execution passed.  RTL failures remain external
IT evidence and are not repaired by this source repository.
