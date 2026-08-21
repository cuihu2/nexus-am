# Poseidon remaining cases: implementation-readiness audit

Date: 2026-08-20

## 2026-08-21 Encode/Rescale integration update

CMB011 and CMB013 are no longer external-algorithm blockers. The vendored
inline-asm package now provides executable N=4096/Q4 Encode and
two-component rounded drop-last Rescale programs, uint32 HPU_MEM images,
independent golden data, and 61/78 concrete DMA relocations respectively.
Both cases execute the generated entries and retain only RTL monitor/cache
evidence requirements. CMB015 is now the only
`BLOCKED_EXTERNAL_ALGORITHM_CONTRACT,HPU_commands_not_issued` testcase.

The older investigation below is retained as historical context and is
superseded for Encode and Rescale by this update.

## 2026-08-21 implementation update

This file preserves the pre-fix investigation below. Two cases previously in
scope are now resolved: CMB014 uses the frozen Galois-element-3 Auto package
with 716 concrete DMA relocations, and APP001 executes the generated
1197-DMA CiphertextMultiply/Relinearization program with deterministic input,
golden, poison and guard checks. They no longer carry external-data/entry
blockers.

The remaining external-algorithm blocker is exactly CMB015 (bootstraping).
Its published artifact is explicitly marked
`BLOCKED_EXTERNAL_ALGORITHM_CONTRACT,HPU_commands_not_issued`; the validator
rejects that marker for every other testcase. Assertions below
about symbolic Auto DMA or a missing application entry describe the audited
2026-08-20 snapshot and are superseded by this update.

This audit covers `HPU_IT_DIR_CMB_011`, `HPU_IT_DIR_CMB_014`,
`HPU_IT_DIR_CMB_015`, and `HPU_IT_DIR_APP_001`.  The acceptance rule is that
an algorithm case may drop `HPU_REQ_EXTERNAL_DATA` and
`HPU_REQ_EXTERNAL_ENTRY` only after it has all of the following in one
reproducible package:

1. deterministic input and parameters;
2. the real HPU instruction sequence for the named algorithm;
3. an independent CPU/reference result;
4. poison/guard validation;
5. a directly executable host and RISC-V entry with all DMA operands bound.

A representative arithmetic or transform sequence is not evidence for a
named Poseidon algorithm.

## Authoritative test rows

The source workbook is
`/home/l/.codex/attachments/2855bcd5-84f3-443a-9a93-8c7783fbb572/HPU-IT测试点分解.xlsx`.
The duplicate attachment under `40813eaf-b19e-4c88-9427-e05799871455` is
byte-identical.  SHA-256:

```text
a483d9b10c8626ced01d9e3cad55ce044e1884980b6c1cc9390d35737129f83d
```

The relevant rows are:

| Case | `测试点分解` row | `测试用例` row | Required real sequence and evidence |
| --- | ---: | ---: | --- |
| CMB011 encode | 31 | 32 | whole NTT -> BConv/ModUp/ModDown -> DSTORE; `ENCODE_GOLDEN`; command/object/line/stage trace; input and guard unchanged |
| CMB014 rotate | 34 | 35 | whole NTT -> Auto -> KeySwitch -> whole INTT -> DSTORE; `ROTATE_GOLDEN`; rotation/Auto/key correspondence; trace and guard |
| CMB015 bootstraping | 35 | 36 | frozen Poseidon stage table using NTT, BConv/ModUp/ModDown, KeySwitch, Auto and INTT -> DSTORE; `BOOTSTRAPING_GOLDEN`; per-stage PSYNC/trace and guard |
| APP001 application | 54 | 51 | a software-team application package with repository revision, submodules, filelist, operator list, runtime, input, DDR layout, golden, completion criterion, seed, IT entry and performance threshold |

The spelling `bootstraping` is preserved because that is the function/test
name in the workbook.

## Assets actually present

### HPU runtime

The IT runtime has a frozen 256-byte-line ABI and a default
`HPU_IT_MEM_LINES=19201` window.  Its delivery-sized N=4096 profile is
`Q=4`, `P=3`, `dnum=2` with 32-bit canonical residues.  The current full FHE
fixture consumes the window exactly:

```text
master       16961 lines
broadcast     1984 lines (31 * 64)
scratch        192 lines (3 * 64)
guard           64 lines
total         19201 lines
```

Therefore a new combined algorithm cannot silently append assets to that
profile.  It must either reuse a proved line layout or provide a newly sized
window plus a capacity proof.  The runtime's current `hpu_fhe_run` only has
program selections for KeySwitch, Relin, CiphertextMultiply and HMul; it has
no encode, rotate, bootstraping, or application program.

### `cuihu2/inline-asm`

The submodule is pinned at
`4399883b9e1fa249b99d48c7e919ee52acc662bc`.  It has real N=4096 generator,
encoder, q32 hardware images and independent reference packages for NTT,
INTT, BConv, ModUp, ModDown and KeySwitch.  Useful generated image sizes are:

| Operator package | HPU_MEM lines |
| --- | ---: |
| NTT | 1025 |
| BConv | 1921 |
| ModUp | 6849 |
| ModDown | 6977 |
| KeySwitch | 9089 |
| CiphertextMultiply | 16961 |

These are independent operator packages, not a connected encode/rotate/
bootstraping package.  Each package still records the same pending hardware
fields: DMA instruction relocation and GPR loading, scratch allocation, and
terminal-PSYNC host handling.

There is no `encode` algorithm generator/output/test-data directory.  The
only `encode/` directory is the textual HPU instruction encoder.  The NTT
fixture and BConv fixture have different standalone inputs; no manifest
asserts that NTT output feeds BConv input or names the result
`ENCODE_GOLDEN`.

`auto.asm` is not executable.  It contains symbolic registers such as
`x_c0`, `x_offset`, `x_out`, and `x_tmp_c0`, has no `.inst32`, and its
`outputs/auto/test_data/STATUS.md` explicitly blocks delivery until physical
register allocation, automorphism index layout, and the DMA ABI are fixed.
Consequently the otherwise usable KeySwitch package is insufficient for a
real rotate sequence.

The pinned submodule contains no root `LICENSE` or `COPYING` file.  The
repository's `THIRD_PARTY_NOTICES.md` already requires the project owner to
confirm authorization before use, modification, or redistribution.  This is
a release gate even after technical integration is complete.

### Public Poseidon software source

The public library named by the workbook is
[`luhang-HPU/poseidon`](https://github.com/luhang-HPU/poseidon).  Its current
`main` is `961df3acfc394b634de3fd846946c851903a0e72`.  The public source removes
one ambiguity--`encode` is CKKS slot encoding--but exposes a specification
conflict with the workbook rather than supplying the missing HPU binding:

- vector `CKKSEncoder::encode` maps at most `N/2` real/complex slots through
  the inverse complex FFT, applies the caller-provided `scale`, rounds, RNS
  decomposes into the selected Q level, and calls `coeff_to_dot()` for the
  per-Q negacyclic NTT;
- scalar `CKKSEncoder::encode(double, ...)` repeats one value in every slot and
  directly fills the corresponding NTT-form Q plaintext with
  `round(value * scale) mod qi`;
- neither implementation invokes BConv, ModUp, or ModDown.

The same result holds in the public `hpu-test`, `HPU-HPJ`, `HPU-YZJ`,
`dev-chip-a`, `dev-chip-b`, and `dev-chip-b-test` branches inspected on
2026-08-20: none contains an encode-specific HPU basis-conversion program or
an `ENCODE_GOLDEN` package.  A full-history search (305 reachable commits) also
finds no encoder change involving raise/mod-up/mod-down/BConv and no
`ENCODE_GOLDEN` occurrence.  The repository has no root `LICENSE`, `COPYING`,
or `NOTICE` file, so its code cannot be vendored into this delivery without an
owner license decision.

The workbook, by contrast, requires `IT-CMB-002` NTT followed by
`IT-CMB-001` BConv/ModUp/ModDown.  The current `IT-CMB-001` reference uses the
frozen approximate Q2-to-P1 sum without an alpha-correction term.  For the
otherwise unambiguous scalar encoding `value=1`, `scale=65536`, equal Q0/Q1
NTT residues of `65536` map to P0 residue `23020317`, not `65536`.  Therefore
the public CKKS API does not determine which correction convention, domain,
destination basis, or final value the workbook expects.  Treating the current
BConv probe as the Poseidon encoder would be a false algorithm binding.

A software-only build of the pinned Poseidon revision was also run with the
exact N=4096, Q4/P3 profile and scalar call `encode(1.0, 65536.0, ...)`.
Poseidon returned one NTT-form Q4 plaintext containing 16384 residues, all
equal to `65536`; it produced no P limbs or conversion stage.  This directly
checks the source reading and the fixture below, but does not resolve the
workbook conflict.

[`scripts/generate_poseidon_encode_scalar_fixture.py`](scripts/generate_poseidon_encode_scalar_fixture.py)
captures the part that *is* uniquely determined.  It generates a deterministic
N=4096/Q4 coefficient input for Poseidon scalar encode `(value=1, scale=2^16)`,
executes an independent full-NTT reference against the physical inline-asm
twiddles, and writes a Q4 NTT golden plus a hashed manifest.  The manifest is
hard-marked `NTT_FIXTURE_ONLY_NOT_CMB011_BINDING` and lists the unresolved
basis-conversion contract, so it is reusable once that contract is signed
without allowing premature promotion of CMB011.

### Public Poseidon hardware package

The official hardware installation guide publishes both a complete
`poseidon-1.0.0_x86_64.deb` and an incremental
`poseidon-hardware-1.0.0_x86_64.deb`.  The packages downloaded from the exact
guide URLs on 2026-08-20 have SHA-256 values:

```text
poseidon-1.0.0_x86_64.deb          c449b8f903d6e1a84bd563cff6d73d4313c6396c09f2827d00acf8f72cef63dc
poseidon-hardware-1.0.0_x86_64.deb 17a7d91a7fa8e5410f9d439e8d93690525421eafb0dc44530409b2a5de61f78e
```

These binaries do not supply the missing encode binding.  The public
`CKKSHardwareApi` header and dynamic symbols expose ciphertext multiply,
rotate, conjugate and sqrt plus key/configuration transfers; there is no
encode method.  `EvaluatorCkksHardware` overrides square, multiply/relinearize,
rotate and conjugate only.  The complete shared library contains the normal
software `CKKSEncoder::encode_internal` symbols separately, consistent with
the public source semantics above.  `ContextDataHardware` can calculate and
return raise/down/rescale conversion parameter arrays, but the package
contains neither an encode caller for those arrays nor their HPU instruction
sequence, operand layout, connected input or `ENCODE_GOLDEN`.

The packages also cannot be treated as an executable fixture.  They contain
headers and shared libraries but no encode example, vector or manifest.  The
shared objects require the vendor DPDK QDMA libraries and a bound hardware
card, and the package metadata does not declare those runtime dependencies.
No license/copyright file is present.  Consequently the parameter-generation
symbols are useful corroborating evidence that conversion is a separate
hardware-context concern, but they do not select which conversion the workbook
means or authorize copying the binary into the IT delivery.

### Poseidon, Trident, and simulator material

The local Poseidon and Trident assets under `/home/l/fhe/体系结构/files`
are papers, not source/application packages.  The Poseidon paper describes
operator reuse and packed bootstrapping but does not provide the required
input packs, golden, HPU ISA stream, DDR map, or IT entry.  The local Trident
PDF is an IEEE author version marked for personal use with republication/
redistribution restrictions; it is not an APP001 software package.

`/home/l/fhe/cfhesim` contains MLIR workload descriptions and an abstract
kernel/cycle lowering.  It cannot be used as an HPU executable:

- `encode` is lowered as an external plaintext object, with no NTT/BConv
  algorithm execution;
- rotate is an abstract Auto/Decomp/ModUp/KeySwitch/ModDown graph, not an HPU
  instruction stream or coefficient golden;
- bootstrap uses a legacy adapter schedule.  Its minimum working width is
  `output_limbs + 2*3 + 9`; an output width of four limbs therefore starts at
  nineteen limbs, while the current executable fixture is only Q4/P3;
- the checkout has no root license file, and its working tree is modified;
- the MLIR benchmarks are simulator inputs, not a repository-frozen
  application binary with an HPU runtime, golden and completion criterion.

## Per-case decision

### CMB011 encode -- external requirements remain

The public Poseidon source now defines CKKS input semantics, but the workbook's
extra basis-conversion stage is not part of that implementation.  Available
pieces therefore prove the Poseidon scalar NTT and basis-conversion operators
separately, but no owner-approved asset defines the vector input, scale/level,
NTT-versus-coefficient-domain boundary, exact/approximate conversion rule,
destination basis, combined stage mapping, connected golden, relocatable HPU
program, or runtime entry.  This case must not execute a PMUL/PADD surrogate
or relabel a standalone BConv probe as encode.

Minimum integration:

1. select and freeze the exact Poseidon overload plus input vector, scale,
   `parms_id`/level, basis, coefficient order and NTT convention;
2. resolve the workbook/public-source conflict by defining why BConv is part
   of encode, whether it consumes coefficient- or NTT-domain data, its
   exact/approximate correction rule, and the final destination basis;
3. add one generator entry that emits the connected NTT -> conversion stream;
4. add an independent CPU encode reference and a single manifest containing
   the input, every stage checkpoint and `ENCODE_GOLDEN`;
5. allocate a 19201-line-compatible DDR layout with poison and guard regions;
6. bind every custom1 `rs1/rs2` line operand in a RISC-V entry and verify the
   host package before removing either external flag.

This is the smallest plausible next algorithm because all component
operators exist, but it is not currently a closed loop.

### CMB014 rotate -- external requirements remain

The blocking dependency is Auto, before the combined sequence can even be
encoded.  Also missing are the rotation offset/Galois-element convention,
automorphism permutation, rotation-key package, connected golden, combined
DDR layout and executable entry.  A plain NTT or transform probe is not a
rotate test.

The public Poseidon software does define slot rotation (step to Galois element,
automorphism and key switch), and its example uses a step-one left rotation as
the decoded oracle.  The public hardware binary also exports an x86/DPDK
`ckks_rotate` card API.  Neither asset provides a RISC-V HPU instruction stream,
physical Auto permutation-table layout, fixed coefficient input/key or
`ROTATE_GOLDEN` for the current N=4096/Q4/P3 profile.  Thus the card API cannot
be linked or relabelled as the workbook's NTT -> Auto -> KeySwitch -> INTT
sequence.

Minimum integration: finish Auto register allocation and index layout; add an
Auto reference/vector package; generate a single NTT -> Auto -> KeySwitch ->
INTT program with concrete DMA operands; then add a CPU rotation oracle,
`ROTATE_GOLDEN`, poison/guard checks and host/RISC-V entry tests.

### CMB015 bootstraping -- external requirements remain

No frozen Poseidon stage table is present.  The simulator's legacy adapter is
neither a Poseidon HPU program nor a golden producer, and its limb demand is
not covered by the Q4/P3 fixture.  Auto is blocked, evaluation keys and
plaintext matrices are absent, and the current 19201-line layout has zero
unallocated lines.

Current public Poseidon `main` now contains a software bootstrap implementation,
but its checked example uses N=65536, at least sixteen Q primes, P=1 and an
expected fourteen-level budget.  The hardware ABI enumerates degrees only
through 32768 and exports no bootstrap method.  That software path can inform a
future CPU oracle after parameters are selected; it is not compatible with or
a capacity proof for the delivery N=4096/Q4/P3 profile.

Minimum integration: obtain/freeze the Poseidon stage table and parameters;
generate CPU checkpoints for ModUp, CoeffToSlot, approximate modular
reduction and SlotToCoeff; provide all rotation/relinearization keys and
plaintext matrices; prove a streamed memory plan under the selected window;
then generate the concrete HPU sequence and executable entry.  Capacity must
be proved rather than inferred from the smaller ciphertext-multiply fixture.

### APP001 -- external requirements remain

No local asset supplies the workbook's mandatory application package,
operator list, executable, frozen repository/submodule/filelist, application
input/golden, completion criterion, IT entry, or performance threshold.
Neither a paper nor a simulator MLIR trace satisfies these fields.  The
existing one-line arithmetic chain is not an end-to-end FHE application.

Minimum integration: the software owner must nominate one terminating small
application and freeze all fields listed in workbook row 51.  Only after its
operators have real runtime implementations should the test add the package
to the build, run it from its actual entry, DSTORE the final result, compare
the application golden and guards, and apply the owner-approved performance
threshold.

## Runtime behavior until integration

All named-algorithm cases without a closed loop should remain buildable
descriptors with their current external-data/entry requirements, but their
probe path must issue no representative HPU algorithm.  This includes the
four cases audited above, CMB005 while Auto is unbound, and CMB013 while only
P3->Q4 ModDown rather than a defined CKKS rescale is available.  The probe
should log `algorithm_binding=missing`, name the missing asset class, state
`hpu_commands=not_issued`, and finish as `PASS_PROBE`/qualification-pending.
A real algorithm path may replace that probe only when the five acceptance
conditions at the top of this document are met.
