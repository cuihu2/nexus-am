#!/usr/bin/env python3
"""Generate the unambiguous NTT half of a Poseidon scalar encode fixture.

The public Poseidon CKKSEncoder::encode(double, ..., scale, ...) overload
represents the same scalar in every CKKS slot.  Its NTT-form RNS plaintext is
therefore the NTT of a coefficient-domain constant polynomial.  This tool
freezes value=1 and scale=2^16, checks the complete inline-asm N=4096/Q4 NTT
against the delivered physical twiddles, and emits deterministic input and
golden images.

This is deliberately *not* a CMB011 binding.  The authoritative workbook adds
BConv/ModUp/ModDown after the NTT, while the public Poseidon encoder does not.
The generated manifest records that unresolved contract so these assets cannot
be promoted as ENCODE_GOLDEN without an owner-approved basis-conversion map.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path


N = 4096
LOG_N = 12
WORDS_PER_LINE = 64
LINE_BYTES = 256
POLY_LINES = N // WORDS_PER_LINE
STAGE_LINES = (N // 2) // WORDS_PER_LINE
Q = (50061313, 50077697, 50307073, 50552833)
P = (90062849, 90095617, 90218497)
SCALAR_VALUE = 1
SCALE = 1 << 16
SCALED_INTEGER = SCALAR_VALUE * SCALE

MASTER_LINES = 16961
MASTER_SHA256 = "601fcddd91fb772d86ce29fad0afc92b1533d2728b1a7413e748b3dc178cd793"
TWIDDLE_BASE_LINE = 10689
TWIDDLE_BASIS_STRIDE = 896
TWIDDLE_NTT_STAGE_OFFSET = 64

POSEIDON_SOURCE_URL = "https://github.com/luhang-HPU/poseidon"
POSEIDON_SOURCE_COMMIT = "961df3acfc394b634de3fd846946c851903a0e72"
INLINE_ASM_SOURCE_URL = "https://github.com/cuihu2/inline-asm"
INLINE_ASM_SOURCE_COMMIT = "4399883b9e1fa249b99d48c7e919ee52acc662bc"


class FixtureError(RuntimeError):
    """A frozen source asset or mathematical invariant did not match."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_u32(data: bytes) -> tuple[int, ...]:
    if len(data) % 4 != 0:
        raise FixtureError("master image length is not a multiple of uint32")
    return struct.unpack(f"<{len(data) // 4}I", data)


def line_words(words: tuple[int, ...], first_line: int, line_count: int) -> tuple[int, ...]:
    first = first_line * WORDS_PER_LINE
    count = line_count * WORDS_PER_LINE
    last = first + count
    if first_line < 0 or line_count <= 0 or last > len(words):
        raise FixtureError(
            f"line span [{first_line}, {first_line + line_count}) is outside master image"
        )
    return words[first:last]


def bit_reverse(values: list[int]) -> None:
    target = 0
    for source in range(1, len(values)):
        bit = len(values) >> 1
        while target & bit:
            target ^= bit
            bit >>= 1
        target ^= bit
        if source < target:
            values[source], values[target] = values[target], values[source]


def inline_asm_ntt(words: tuple[int, ...], basis: int, coefficient: list[int]) -> list[int]:
    modulus = Q[basis]
    if len(coefficient) != N:
        raise FixtureError(f"basis {basis}: expected {N} coefficients")
    if any(value < 0 or value >= modulus for value in coefficient):
        raise FixtureError(f"basis {basis}: non-canonical coefficient")

    basis_base = TWIDDLE_BASE_LINE + basis * TWIDDLE_BASIS_STRIDE
    pre_twist = line_words(words, basis_base, POLY_LINES)
    work = [(value * pre_twist[index]) % modulus for index, value in enumerate(coefficient)]
    bit_reverse(work)

    for stage in range(LOG_N):
        twiddle = line_words(
            words,
            basis_base + TWIDDLE_NTT_STAGE_OFFSET + stage * STAGE_LINES,
            STAGE_LINES,
        )
        half = 1 << stage
        group = half << 1
        twiddle_index = 0
        next_values = work.copy()
        for base in range(0, N, group):
            for offset in range(half):
                even = work[base + offset]
                odd = (
                    work[base + offset + half] * twiddle[twiddle_index]
                ) % modulus
                twiddle_index += 1
                next_values[base + offset] = (even + odd) % modulus
                next_values[base + offset + half] = (even - odd) % modulus
        if twiddle_index != N // 2:
            raise FixtureError(f"basis {basis} stage {stage}: incomplete twiddle consumption")
        work = next_values
    return work


def pack_u32(values: list[int]) -> bytes:
    return struct.pack(f"<{len(values)}I", *values)


def write_if_different(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == data:
        return
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def generate(master_path: Path, output_dir: Path) -> dict[str, object]:
    master = master_path.read_bytes()
    actual_master_sha = sha256_bytes(master)
    if actual_master_sha != MASTER_SHA256:
        raise FixtureError(
            "inline-asm master image SHA-256 mismatch: "
            f"expected {MASTER_SHA256}, got {actual_master_sha}"
        )
    if len(master) != MASTER_LINES * LINE_BYTES:
        raise FixtureError(
            f"inline-asm master image has {len(master)} bytes, "
            f"expected {MASTER_LINES * LINE_BYTES}"
        )
    words = read_u32(master)

    input_values: list[int] = []
    golden_values: list[int] = []
    for basis, modulus in enumerate(Q):
        coefficient = [0] * N
        coefficient[0] = SCALED_INTEGER % modulus
        transformed = inline_asm_ntt(words, basis, coefficient)
        expected = SCALED_INTEGER % modulus
        if any(value != expected for value in transformed):
            first_bad = next(
                index for index, value in enumerate(transformed) if value != expected
            )
            raise FixtureError(
                f"basis {basis}: scalar NTT mismatch at coefficient {first_bad}: "
                f"got {transformed[first_bad]}, expected {expected}"
            )
        input_values.extend(coefficient)
        golden_values.extend(transformed)

    input_blob = pack_u32(input_values)
    golden_blob = pack_u32(golden_values)
    files = {
        "input_coeff_q.u32.bin": input_blob,
        "expected_ntt_q.u32.bin": golden_blob,
    }
    for name, blob in files.items():
        write_if_different(output_dir / name, blob)

    manifest: dict[str, object] = {
        "format_version": 1,
        "status": "NTT_FIXTURE_ONLY_NOT_CMB011_BINDING",
        "case_id": "HPU_IT_DIR_CMB_011",
        "algorithm": "Poseidon.CKKSEncoder.scalar_encode",
        "semantic_source": {
            "repository": POSEIDON_SOURCE_URL,
            "commit": POSEIDON_SOURCE_COMMIT,
            "api": "CKKSEncoder::encode(double, parms_id_type, double, Plaintext&)",
            "contract": "one scalar repeated in all N/2 CKKS slots; output is NTT-form RNS plaintext",
            "license_status": "NO_ROOT_LICENSE_FOUND_OWNER_REVIEW_REQUIRED",
        },
        "hpu_ntt_source": {
            "repository": INLINE_ASM_SOURCE_URL,
            "commit": INLINE_ASM_SOURCE_COMMIT,
            "master_image": str(master_path),
            "master_image_sha256": actual_master_sha,
            "convention": "negacyclic pre-twist, radix-2 DIT, natural-order output",
        },
        "parameters": {
            "scheme": "CKKS",
            "N": N,
            "slots": N // 2,
            "value": SCALAR_VALUE,
            "scale": SCALE,
            "scaled_integer": SCALED_INTEGER,
            "q": list(Q),
            "p": list(P),
            "coefficient_encoding": "little-endian uint32 canonical residue",
            "basis_order": "Q0,Q1,Q2,Q3",
            "line_bytes": LINE_BYTES,
            "lines_per_polynomial": POLY_LINES,
        },
        "layout": {
            "input_coeff_q.u32.bin": "Q4xN coefficient-domain input; only coefficient zero is scale",
            "expected_ntt_q.u32.bin": "Q4xN NTT-form golden; every coefficient is scale",
        },
        "files": {
            name: {"bytes": len(blob), "sha256": sha256_bytes(blob)}
            for name, blob in files.items()
        },
        "unresolved_cmb011_contract": [
            "owner-approved definition of the workbook BConv/ModUp/ModDown stage",
            "whether basis conversion consumes coefficient-domain or NTT-domain values",
            "exact source and destination bases and level",
            "exact versus approximate basis-conversion correction convention",
            "final output basis and ENCODE_GOLDEN identity",
            "terminal-only PSYNC waiver versus workbook stage-PSYNC wording",
            "redistribution authorization for the public Poseidon source",
        ],
    }
    manifest_blob = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    write_if_different(output_dir / "manifest.json", manifest_blob)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    repo_root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--master-image",
        type=Path,
        default=repo_root
        / "third_party/inline-asm/outputs/ciphertext_multiply/test_data/hardware"
        / "hpu_mem_image.u32.bin",
    )
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        manifest = generate(arguments.master_image.resolve(), arguments.output.resolve())
    except (FixtureError, OSError, struct.error) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(
        "PASS: generated deterministic Poseidon scalar-encode NTT fixture; "
        f"status={manifest['status']} output={arguments.output.resolve()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
