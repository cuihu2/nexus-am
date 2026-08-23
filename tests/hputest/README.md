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
```

Each source has its own `main()` and shows the complete testcase sequence:
CSR programming, HPU command arguments, polling, self-check, and return code.
Headers under `include/hpu/` contain only one-operation hardware adapters;
there is no testcase-kind dispatcher or centralized scenario implementation.

## Smoke suite 001

| Source | Operation | Completion |
|---|---|---|
| `01_dload_hold.c` | DLOAD then `while (1)` | Intentional waveform hold |
| `02_read_csr_status.c` | Program/read HPU CSR state | Return 0/nonzero |
| `03_dload_poll_csr.c` | DLOAD and poll STATUS busy | Return 0/nonzero |
| `04_psync_irq.c` | PSYNC and PLIC source 257 | Return 0/nonzero |
| `05_dload_psync.c` | DLOAD then terminal PSYNC | Return 0/nonzero |
| `06_dload_dstore_psync.c` | DMA loopback and word self-check | Return 0/nonzero |
| `07_dload_compute_dstore_psync.c` | PADD program and modular golden check | Return 0/nonzero |

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
CSR base `0x08000000`, HPU memory base `0x87000000`, and a 64-line bring-up
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
