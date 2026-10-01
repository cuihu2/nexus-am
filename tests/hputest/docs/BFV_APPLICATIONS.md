# BFV application delivery

APP004 and APP005 consume `bfv_multiply_modswitch_application` and
`bfv_rotation_application` from the pinned
`third_party/hpu-applications` gitlink through application package v1.
`prepare-inline-asm-mm.sh` builds the upstream delivery target with SEAL
integration, runs `hpu_validate_package`, and imports each validated program,
initial image, all step goldens, and writable-line mask.

APP004 uses BFV N=128 and the graph `Multiply+Relinearize -> ModSwitch ->
AddPlain(3)`. It moves Q3 to Q2, contains 4434 instructions and 1835 resolved
DMA operations, and uses 692 HPU_MEM lines in a 16384-line capacity. Its 14
coefficient-domain golden limbs cover six Q3 limbs after multiplication and
four Q2 limbs after each remaining step.

APP005 uses BFV N=128, Q3, two ciphertext components, and the graph
`RotateRows(x,2) -> RotateColumns(x) -> Add`. The two rotations are independent
branches over the same input and each performs Galois KeySwitch before the final
add. The package contains 2633 instructions, 1057 resolved DMA operations, 464
used HPU_MEM lines in an 8192-line capacity, and 18 coefficient-domain golden
limbs (three steps x two components x three moduli).

The producer must report passing modified-SEAL and `BfvSoftwareExecutor`
word-for-word checks while leaving instruction/RTL/hardware verification false.
The AM importer rejects schema, parameter, graph, instruction, allocation, DMA,
golden-domain, provenance, and oracle drift. It poisons every output allocation,
adds a 64-line tail guard, and emits a per-line DSTORE permission mask.

On target, each case loads its imported image, runs the generated resolved
program, waits for the terminal PSYNC interrupt, checks every intermediate/final
limb and padding word, and verifies that all non-DSTORE lines plus the tail guard
are unchanged. Build success is `BUILD_READY_NOT_IT_PASS`; actual RTL/IT
execution and further parameters, levels, rotations, and boundary values remain
separate work.
