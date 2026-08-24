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
│   ├── 04_psync_irq.c
│   ├── 05_dload_psync.c
│   ├── 06_dload_dstore_psync.c
│   └── 07_dload_compute_dstore_psync.c
└── 002_main_return_probe/
    ├── 01_return_0.c
    └── 02_return_1.c

src/common/
└── hpu_rns_fixture.S
```

Each source has its own `main()` and shows the complete testcase sequence:
CSR programming, HPU command arguments, polling, self-check, and return code.
Headers under `include/hpu/` contain only one-operation hardware adapters;
there is no testcase-kind dispatcher or centralized scenario implementation.

## Smoke suite 001

| Source | Operation | Completion |
|---|---|---|
| `01_dload_hold.c` | DLOAD then `while (1)` | Intentional waveform hold |
| `02_read_csr_status.c` | Program/read HPU CSR state | Return 0/1 |
| `03_dload_poll_csr.c` | 64-line DLOAD and poll STATUS busy | Return 0/1 |
| `04_psync_irq.c` | PSYNC and PLIC source 257 | Return 0/1 |
| `05_dload_psync.c` | 64-line DLOAD then terminal PSYNC | Return 0/1 |
| `06_dload_dstore_psync.c` | 4096-coefficient DMA loopback self-check | Return 0/1 |
| `07_dload_compute_dstore_psync.c` | 4096-coefficient PADD/C golden check | Return 0/1 |

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
Mismatch and all other detected failures are logged with a stage/detail and
returned from `main()` as 1; a completed self-check returns 0.

## What the CSR checks prove

The smoke sequence deliberately checks configuration in layers:

1. Write and read back `BASE_LO/HI` and `SIZE_LO/HI`.  This proves that CPU
   MMIO writes reached the shadow CSRs.
2. Write `COMMIT`, then poll `STATUS.window_valid`.  This proves that the
   nonzero window configuration was accepted.  `COMMIT` itself is a pulse and
   is not checked by reading back a value of 1.
3. Require `STATUS.busy=0`, `STATUS.fault_valid=0`, FAULT clear, and IRQ clear
   before issuing work.
4. Case 03 observes DLOAD `busy` go high and then low; case 05 observes PSYNC
   completion.  These prove more than CSR readback, but not payload accuracy.
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
