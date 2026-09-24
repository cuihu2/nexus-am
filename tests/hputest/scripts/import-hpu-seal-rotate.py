#!/usr/bin/env python3
"""Validate and import the HPU_SEAL BFV RotateRows package used by CMB014."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


N = 4096
COMPONENTS = 2
Q_MODULI = (133881857, 133963777, 134012929, 134111233)
Q_COUNT = len(Q_MODULI)
PLAIN_MODULUS = 114689
ROTATION_STEPS = 1
GALOIS_ELEMENT = 3
LINE_BYTES = 256
WORDS_PER_LINE = LINE_BYTES // 4
POLY_LINES = N // WORDS_PER_LINE
OUTPUT_LINES = COMPONENTS * Q_COUNT * POLY_LINES
GUARD_LINES = 64
EXPECTED_INSTRUCTIONS = 2593
EXPECTED_DMA = 997
EXPECTED_ALLOCATIONS = 484
ACTIVE_LINES = 20098
OUTPUT_OFFSET = 19586
TOTAL_LINES = 20162
OUTPUT_POISON = 0xC0140000
GUARD_POISON = 0xA5A51400
API = "hpu::seal_adapter::BfvOperationPlan::append_rotate_rows"
WINDOW_SHA256 = "17be6ed83a597d429f416d1ef3ef33fd85fe1a560c6431739830a4302814c078"
GOLDEN_SHA256 = "1ff3a3c363e9aea740f6fd9dc1ceabb868d9552b2adcef8a362bb260cc0b4f28"

REQUIRED_FILES = (
    "rotate.asm",
    "rotate.c",
    "rotate.h",
    "rotate.inst32",
    "rotate.cmd26",
    "resolved_dma.csv",
    "allocations.csv",
    "metadata.json",
    "producer_commit.txt",
    "window.u32.bin",
    "golden.u32.bin",
    "input_slots.u64.bin",
    "expected_slots.u64.bin",
    "HOST_ORACLE.log",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def words(raw: bytes) -> tuple[int, ...]:
    require(len(raw) % 4 == 0, "uint32 payload has a partial word")
    return struct.unpack(f"<{len(raw) // 4}I", raw)


def slots(raw: bytes) -> tuple[int, ...]:
    require(len(raw) == N * 8, "BFV slot payload must contain exactly N uint64 values")
    return struct.unpack(f"<{N}Q", raw)


def parse_program(path: Path) -> list[str]:
    program = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.search(r'"(.*?)\\n\\t"', line)
        if match:
            program.append(match.group(1).strip())
    return program


def validate_metadata(source: Path, producer_commit: str) -> dict[str, object]:
    metadata = json.loads((source / "metadata.json").read_text(encoding="utf-8"))
    expected = {
        "format_version": 1,
        "case_id": "HPU_IT_DIR_CMB_014",
        "scheme": "BFV",
        "api": API,
        "producer_commit": producer_commit,
        "poly_modulus_degree": N,
        "component_count": COMPONENTS,
        "q_count": Q_COUNT,
        "q_moduli": list(Q_MODULI),
        "plain_modulus": PLAIN_MODULUS,
        "domain": "coefficient",
        "rotation_steps": ROTATION_STEPS,
        "galois_element": GALOIS_ELEMENT,
        "instruction_count": EXPECTED_INSTRUCTIONS,
        "dma_count": EXPECTED_DMA,
        "active_lines": ACTIVE_LINES,
        "output_offset": OUTPUT_OFFSET,
        "output_lines": OUTPUT_LINES,
        "guard_offset": ACTIVE_LINES,
        "guard_lines": GUARD_LINES,
        "total_lines": TOTAL_LINES,
    }
    for key, value in expected.items():
        require(metadata.get(key) == value,
                f"Rotate metadata {key!r} differs from the fixed CMB014 contract")
    require((source / "producer_commit.txt").read_text(encoding="ascii").strip()
            == producer_commit, "Rotate producer commit marker differs from metadata")
    return metadata


def validate_allocations(source: Path) -> dict[str, dict[str, str]]:
    allocation_rows = rows(source / "allocations.csv")
    allocations = {entry["id"]: entry for entry in allocation_rows}
    require(len(allocation_rows) == EXPECTED_ALLOCATIONS
            and len(allocations) == len(allocation_rows),
            "Rotate allocation count or ids differ from the fixed layout")
    cursor = 0
    valid_kinds = {
        "modulus_table", "constant", "ciphertext", "evaluation_key",
        "twiddle", "workspace", "output",
    }
    for entry in sorted(allocation_rows, key=lambda item: int(item["line_offset"])):
        offset = int(entry["line_offset"])
        count = int(entry["line_count"])
        word_count = int(entry["word_count"])
        require(offset == cursor and count > 0 and 0 < word_count <= count * WORDS_PER_LINE,
                f"Rotate allocation {entry['id']} has a gap or invalid geometry")
        require(entry["kind"] in valid_kinds and entry["read_only"] in ("0", "1"),
                f"Rotate allocation {entry['id']} has invalid permissions or kind")
        cursor += count
    require(cursor == ACTIVE_LINES,
            "Rotate allocation table does not exactly cover the active image")

    table = allocations.get("constants/modulus_table")
    require(table is not None and table["line_offset"] == "0"
            and table["line_count"] == "1" and table["read_only"] == "1",
            "Rotate modulus-table allocation is invalid")
    for component in range(COMPONENTS):
        for basis in range(Q_COUNT):
            input_item = allocations.get(f"input/x/c{component}/mod{basis}")
            scratch_item = allocations.get(f"scratch/rotate_rows_1/c{component}/mod{basis}")
            output_item = allocations.get(f"output/rotate/c{component}/mod{basis}")
            require(input_item is not None and input_item["kind"] == "ciphertext"
                    and input_item["read_only"] == "1"
                    and int(input_item["word_count"]) == N,
                    "Rotate input ciphertext allocation is invalid")
            require(scratch_item is not None and scratch_item["read_only"] == "0"
                    and int(scratch_item["word_count"]) == N,
                    "Rotate coefficient workspace allocation is invalid")
            expected_line = OUTPUT_OFFSET + (component * Q_COUNT + basis) * POLY_LINES
            require(output_item is not None and output_item["kind"] == "output"
                    and output_item["read_only"] == "0"
                    and int(output_item["line_offset"]) == expected_line
                    and int(output_item["line_count"]) == POLY_LINES
                    and int(output_item["word_count"]) == N,
                    "Rotate final output allocation is invalid")
    require(any(name.startswith("key/rotate_rows_1/top/")
                and entry["kind"] == "evaluation_key"
                for name, entry in allocations.items()),
            "Rotate Galois-key allocations are missing")
    require(any(name.startswith("constants/twiddle/rotate_rows_1/top/")
                and entry["kind"] == "twiddle"
                for name, entry in allocations.items()),
            "Rotate galois-bound fused twiddles are missing")
    return allocations


def validate_slots(source: Path) -> tuple[bytes, bytes]:
    input_raw = (source / "input_slots.u64.bin").read_bytes()
    expected_raw = (source / "expected_slots.u64.bin").read_bytes()
    input_values, expected_values = slots(input_raw), slots(expected_raw)
    row_size = N // 2
    for index, value in enumerate(input_values):
        require(value == (3 * index * index + 5 * index + 7) % PLAIN_MODULUS,
                f"Rotate input slot {index} differs from the fixed formula")
        row = index // row_size
        column = index % row_size
        rotated = input_values[row * row_size + (column + ROTATION_STEPS) % row_size]
        require(expected_values[index] == rotated,
                f"Rotate expected slot {index} is not a left rotation by one")
    return input_raw, expected_raw


def validate_window(source: Path) -> tuple[bytes, bytes]:
    window_raw = (source / "window.u32.bin").read_bytes()
    golden_raw = (source / "golden.u32.bin").read_bytes()
    require(len(window_raw) == TOTAL_LINES * LINE_BYTES,
            "Rotate window size differs from the fixed layout")
    require(len(golden_raw) == COMPONENTS * Q_COUNT * N * 4,
            "Rotate golden size differs from two Q4 ciphertext components")
    require(hashlib.sha256(window_raw).hexdigest() == WINDOW_SHA256,
            "Rotate deterministic HPU_MEM image hash differs from the reviewed package")
    require(hashlib.sha256(golden_raw).hexdigest() == GOLDEN_SHA256,
            "Rotate deterministic SEAL golden hash differs from the reviewed package")
    window, golden = words(window_raw), words(golden_raw)
    output_first = OUTPUT_OFFSET * WORDS_PER_LINE
    output_count = OUTPUT_LINES * WORDS_PER_LINE
    require(all(value == (OUTPUT_POISON ^ index)
                for index, value in enumerate(window[output_first:output_first + output_count])),
            "Rotate final output lacks the fixed poison pattern")
    guard_first = ACTIVE_LINES * WORDS_PER_LINE
    require(all(value == (GUARD_POISON ^ index)
                for index, value in enumerate(window[guard_first:])),
            "Rotate tail guard pattern is invalid")
    modulus_table = window[:WORDS_PER_LINE]
    for basis, modulus in enumerate(Q_MODULI):
        mu = (1 << 64) // modulus
        require(modulus_table[basis * 4:basis * 4 + 4]
                == (modulus, mu & 0xFFFFFFFF, mu >> 32, 0),
                f"Rotate modulus-table record {basis} has invalid q/mu")
        for component in range(COMPONENTS):
            start = (component * Q_COUNT + basis) * N
            require(all(value < modulus for value in golden[start:start + N]),
                    "Rotate golden coefficient exceeds its modulus")
    return window_raw, golden_raw


def validate_program(source: Path, encoder: Path,
                     allocations: dict[str, dict[str, str]]) -> tuple[list[str], list[int], bytes]:
    subprocess.run([str(encoder), str(source / "rotate.asm"),
                    str(source / "rotate.inst32"), str(source / "rotate.cmd26")],
                   check=True)
    program = parse_program(source / "rotate.asm")
    encoded = [int(bits, 2) for bits in (source / "rotate.inst32").read_text().split()]
    commands = [int(bits, 2) for bits in (source / "rotate.cmd26").read_text().split()]
    require(len(program) == len(encoded) == len(commands) == EXPECTED_INSTRUCTIONS,
            "Rotate program length differs from the fixed HPU_SEAL sequence")
    require(program[0] == "dload x10, x11, p4, 2, 1"
            and program[-2:] == ["pfree p4", "psync"]
            and program.count("psync") == 1,
            "Rotate program lacks one enclosing modulus load and terminal PSYNC")
    operation_counts = {name: sum(item.startswith(name + " ") or item == name
                                  for item in program)
                        for name in ("pntt", "pintt", "pmul", "pmac", "padd", "psub")}
    require(operation_counts == {"pntt": 336, "pintt": 216, "pmul": 94,
                                 "pmac": 30, "padd": 14, "psub": 8},
            "Rotate arithmetic sequence differs from reviewed Auto+Galois KeySwitch")
    require(commands == [(word >> 7) | ((1 << 25) if word & 0x7F == 0x2B else 0)
                         for word in encoded],
            "Rotate inst32/cmd26 streams are inconsistent")

    generated_c = (source / "rotate.c").read_text(encoding="utf-8")
    source_words = [int(value, 16)
                    for value in re.findall(r'\.word 0x([0-9A-Fa-f]{8})', generated_c)]
    source_program = [asm for _, asm in re.findall(r'/\* (\d+): (.*?) \*/', generated_c)]
    require(source_words == encoded and source_program == program,
            "Rotate generated C differs from ASM/inst32")
    require(f"#define HPU_MEM_LINE_COUNT UINT64_C({TOTAL_LINES})" in generated_c
            and "int hpu_run_rotate(void)" in generated_c,
            "Rotate generated C lacks the fixed window or wrapper")
    generated_h = (source / "rotate.h").read_text(encoding="utf-8")
    require(f"HPU_PROGRAM_ROTATE_DMA_COUNT = {EXPECTED_DMA}" in generated_h
            and "int hpu_run_rotate(void);" in generated_h,
            "Rotate generated header has the wrong runtime contract")

    resolved = rows(source / "resolved_dma.csv")
    require(len(resolved) == EXPECTED_DMA,
            "Rotate resolved DMA schedule has the wrong row count")
    dma_instructions = [index for index, asm in enumerate(program)
                        if asm.startswith(("dload", "dstore"))]
    require(len(dma_instructions) == EXPECTED_DMA,
            "Rotate program has the wrong DMA instruction count")
    writable_lines: set[int] = set()
    first_access: dict[str, str] = {}
    for dma_index, (entry, instruction_index) in enumerate(zip(resolved, dma_instructions)):
        allocation_id = entry["allocation_id"]
        require(int(entry["dma_index"]) == dma_index
                and int(entry["instruction_index"]) == instruction_index
                and entry["operation_id"] in ("$application", "rotate_rows_1")
                and entry["normalized_asm"] == program[instruction_index]
                and int(entry["word_hex"], 0) == encoded[instruction_index]
                and allocation_id in allocations,
                f"Rotate resolved DMA row {dma_index} differs from the encoded program")
        allocation = allocations[allocation_id]
        first = int(entry["line_offset"])
        count = int(entry["line_count"])
        require(first == int(allocation["line_offset"])
                and count == int(allocation["line_count"]),
                f"Rotate resolved DMA row {dma_index} differs from its allocation")
        direction = entry["direction"]
        require(direction in ("dload", "dstore")
                and program[instruction_index].startswith(direction),
                f"Rotate resolved DMA row {dma_index} has the wrong direction")
        first_access.setdefault(allocation_id, direction)
        if direction == "dstore":
            require(allocation["read_only"] == "0",
                    f"Rotate DSTORE targets read-only allocation {allocation_id}")
            writable_lines.update(range(first, first + count))
    final_ids = [f"output/rotate/c{component}/mod{basis}"
                 for component in range(COMPONENTS) for basis in range(Q_COUNT)]
    require(all(first_access.get(name) == "dstore" for name in final_ids),
            "Rotate final output is missing or read before its first write")
    require(not any(line >= ACTIVE_LINES for line in writable_lines),
            "Rotate DMA writes outside the active HPU_MEM image")
    mask = bytes(int(line in writable_lines) for line in range(TOTAL_LINES))
    require(not any(mask[ACTIVE_LINES:]), "Rotate tail guard is marked writable")
    return program, encoded, mask


def prepare(source: Path, producer_commit: str, encoder: Path) -> dict[str, object]:
    for relative in REQUIRED_FILES:
        path = source / relative
        require(path.is_file() and path.stat().st_size > 0,
                f"missing or empty Rotate producer file: {relative}")
    oracle = (source / "HOST_ORACLE.log").read_text(encoding="utf-8")
    require("slot_oracle=PASS ciphertext_oracle=PASS software_executor=PASS" in oracle,
            "Rotate host oracle did not report all three validations")
    metadata = validate_metadata(source, producer_commit)
    allocations = validate_allocations(source)
    input_slots, expected_slots = validate_slots(source)
    window, golden = validate_window(source)
    program, encoded, writable = validate_program(source, encoder, allocations)
    return {"metadata": metadata, "allocations": allocations,
            "input_slots": input_slots, "expected_slots": expected_slots,
            "window": window, "golden": golden, "program": program,
            "encoded": encoded, "writable": writable}


def publish(source: Path, destination: Path, prepared: dict[str, object],
            producer_commit: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".rotate-data-", dir=destination.parent))
    try:
        for relative in REQUIRED_FILES:
            shutil.copyfile(source / relative, staging / relative)
        (staging / "rotate_writable.u8.bin").write_bytes(prepared["writable"])
        (staging / "rotate_layout.h").write_text(
            "#ifndef HPU_ROTATE_LAYOUT_H\n#define HPU_ROTATE_LAYOUT_H\n\n"
            f"#define HPU_ROTATE_N {N}U\n"
            f"#define HPU_ROTATE_COMPONENTS {COMPONENTS}U\n"
            f"#define HPU_ROTATE_Q_COUNT {Q_COUNT}U\n"
            f"#define HPU_ROTATE_POLY_LINES {POLY_LINES}U\n"
            f"#define HPU_ROTATE_STEPS {ROTATION_STEPS}\n"
            f"#define HPU_ROTATE_GALOIS_ELEMENT {GALOIS_ELEMENT}U\n"
            f"#define HPU_ROTATE_ACTIVE_LINES {ACTIVE_LINES}U\n"
            f"#define HPU_ROTATE_OUTPUT_OFFSET {OUTPUT_OFFSET}U\n"
            f"#define HPU_ROTATE_OUTPUT_LINES {OUTPUT_LINES}U\n"
            f"#define HPU_ROTATE_GUARD_OFFSET {ACTIVE_LINES}U\n"
            f"#define HPU_ROTATE_GUARD_LINES {GUARD_LINES}U\n"
            f"#define HPU_ROTATE_TOTAL_LINES {TOTAL_LINES}U\n"
            f"#define HPU_ROTATE_INSTRUCTION_COUNT {EXPECTED_INSTRUCTIONS}U\n"
            f"#define HPU_ROTATE_DMA_COUNT {EXPECTED_DMA}U\n"
            + "".join(f"#define HPU_ROTATE_Q{index} {modulus}U\n"
                      for index, modulus in enumerate(Q_MODULI))
            + "\n#endif\n", encoding="utf-8")
        (staging / "rotate_delivery.h").write_text(
            "#ifndef HPU_ROTATE_DELIVERY_H\n#define HPU_ROTATE_DELIVERY_H\n\n"
            "#include \"rotate.h\"\n#include \"rotate_layout.h\"\n\n"
            "#endif\n", encoding="utf-8")
        writable_lines = sum(prepared["writable"])
        (staging / "DELIVERY_SUMMARY.md").write_text(
            "# HPU_SEAL BFV RotateRows AM import\n\n"
            f"- producer commit: `{producer_commit}`\n"
            f"- API: `{API}`\n- scheme/domain: `BFV/coefficient`\n"
            f"- N/Q/components: `{N}/{Q_COUNT}/{COMPONENTS}`\n"
            f"- rotation/galois element: `+{ROTATION_STEPS}/{GALOIS_ELEMENT}`\n"
            f"- instructions/resolved DMA: `{EXPECTED_INSTRUCTIONS}/{EXPECTED_DMA}`\n"
            f"- active/output/guard/total lines: `{ACTIVE_LINES}/"
            f"{OUTPUT_LINES}/{GUARD_LINES}/{TOTAL_LINES}`\n"
            f"- writable/read-only lines: `{writable_lines}/{TOTAL_LINES - writable_lines}`\n"
            f"- window SHA-256: `{WINDOW_SHA256}`\n"
            f"- golden SHA-256: `{GOLDEN_SHA256}`\n",
            encoding="utf-8")
        if destination.exists():
            require(destination.is_dir() and not destination.is_symlink()
                    and (destination / "DELIVERY_SUMMARY.md").is_file(),
                    "refusing to replace an unowned Rotate delivery directory")
            shutil.rmtree(destination)
        staging.rename(destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--producer-commit", required=True)
    parser.add_argument("--encoder", required=True, type=Path)
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit) is not None,
            "producer commit must be a full lowercase SHA-1")
    source, destination = args.source.resolve(), args.destination.resolve()
    require(source.is_dir() and args.encoder.is_file(),
            "Rotate source package or encoder is missing")
    require(source != destination and source not in destination.parents
            and destination not in source.parents, "Rotate source and destination overlap")
    prepared = prepare(source, args.producer_commit, args.encoder.resolve())
    publish(source, destination, prepared, args.producer_commit)
    print(f"HPU_SEAL BFV RotateRows: {EXPECTED_INSTRUCTIONS} instructions, "
          f"{EXPECTED_DMA} resolved DMA, {TOTAL_LINES} HPU lines imported")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, json.JSONDecodeError,
            subprocess.CalledProcessError) as error:
        raise SystemExit(f"HPU_SEAL Rotate import failed: {error}") from error
