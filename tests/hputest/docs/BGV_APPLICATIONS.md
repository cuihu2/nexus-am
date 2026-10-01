# BGV application package and APP006

APP006 consumes `bgv_plain_chain` from the pinned `third_party/hpu-applications`
gitlink.  It does not reinterpret the older `hpu-seal` checkout and does not
copy an unvalidated `outputs/` directory from a developer worktree.

The producer publishes application package schema v1.  `prepare-inline-asm-mm.sh`
builds the `bgv_plain_chain_delivery` target with SEAL integration, records the
exact clean producer commit, runs `hpu_validate_package`, and then imports the
package.  The upstream publisher runs on the WSL POSIX filesystem because its
atomic directory replacement is not supported reliably by DrvFS; the validated
result is copied into the normal workspace build directory.

The accepted fixed profile is:

- BGV, polynomial modulus degree 128, plaintext modulus 65537;
- active ciphertext basis Q3 with moduli 2013265921, 1811939329 and 469762049;
- `add_plain`, `multiply_plain`, then `subtract_plain`;
- two ciphertext components, three moduli and three checked steps: 18 limbs;
- 90 instructions, 46 DMA bindings, one terminal PSYNC;
- 67 used input/output lines plus a 64-line AM guard.

Two independent producer checks are required before AM accepts the package:
the modified-SEAL ciphertext is the golden source, and `BgvSoftwareExecutor`
must reproduce all raw physical words.  The package explicitly does not claim
instruction, RTL or hardware execution.  APP006 supplies that missing execution
step: it poisons all write-first output spans, runs the generated C program once,
compares all intermediate and final canonical NTT/RNS words exactly, and checks
that every non-DSTORE line and the tail guard remain unchanged.

`scripts/test-application-delivery.py` also proves the upstream validator rejects
a corrupted golden and compiles the real target-side checking code into a host
harness covering missing execution, output mismatch, read-only corruption,
guard corruption and permitted DSTORE changes.
