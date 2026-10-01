# BGV application packages: APP006 and APP007

APP006 and APP007 consume `bgv_plain_chain` and `bgv_rotate_chain` from the pinned `third_party/hpu-applications`
gitlink.  It does not reinterpret the older `hpu-seal` checkout and does not
copy an unvalidated `outputs/` directory from a developer worktree.

The producer publishes application package schema v1.  `prepare-inline-asm-mm.sh`
builds both delivery targets with SEAL integration, records the
exact clean producer commit, runs `hpu_validate_package`, and then imports the
package.  The upstream publisher runs on the WSL POSIX filesystem because its
atomic directory replacement is not supported reliably by DrvFS; the validated
result is copied into the normal workspace build directory.

The shared fixed profile is BGV, polynomial modulus degree 128, plaintext
modulus 65537, with active ciphertext basis Q3 using moduli 2013265921,
1811939329 and 469762049. The cases then cover:

- APP006: `add_plain`, `multiply_plain`, then `subtract_plain`; 90 instructions,
  46 DMA bindings and 67 used lines.
- APP007: `add_plain(3)`, `rotate_rows(+1)`, then `add_plain(5)`; 1141
  instructions, 433 DMA bindings and 270 used lines.
- Both cases retain two ciphertext components, three moduli, three checked
  steps (18 limbs), one terminal PSYNC and a 64-line AM guard.

Two independent producer checks are required before AM accepts the package:
the modified-SEAL ciphertext is the golden source, and `BgvSoftwareExecutor`
must reproduce all raw physical words.  The package explicitly does not claim
instruction, RTL or hardware execution.  APP006 and APP007 supply that missing
execution step: each poisons all write-first output spans, runs the generated C program once,
compares all intermediate and final canonical NTT/RNS words exactly, and checks
that every non-DSTORE line and the tail guard remain unchanged.

`scripts/test-application-delivery.py` also proves the upstream validator rejects
a corrupted golden and compiles the real target-side checking code into a host
harness covering missing execution, output mismatch, read-only corruption,
guard corruption and permitted DSTORE changes.
