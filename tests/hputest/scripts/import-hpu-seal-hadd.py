#!/usr/bin/env python3
"""Validate and import the HPU_SEAL BFV HADD package used by CMB009."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile

from program_encoding import check_program


N = 4096
COMPONENTS = 2
Q_COUNT = 4
LINE_BYTES = 256
WORDS_PER_LINE = LINE_BYTES // 4
POLY_LINES = N // WORDS_PER_LINE
OUTPUT_LINES = COMPONENTS * Q_COUNT * POLY_LINES
GUARD_LINES = 64
EXPECTED_INSTRUCTIONS = 59
EXPECTED_DMA = 25
OUTPUT_POISON = 0xC0DE0000
GUARD_POISON = 0xA5A50000
API = "hpu::seal_adapter::BfvOperationPlan::append_add"

REQUIRED_FILES = (
    "hadd.asm",
    "hadd.c",
    "hadd.h",
    "hadd.inst32",
    "hadd.cmd26",
    "resolved_dma.csv",
    "allocations.csv",
    "metadata.json",
    "producer_commit.txt",
    "window.u32.bin",
    "golden.u32.bin",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def fnv1a64(data: bytes) -> int:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = value * 1099511628211 & 0xFFFFFFFFFFFFFFFF
    return value


def words(raw: bytes) -> tuple[int, ...]:
    require(len(raw) % 4 == 0, "uint32 payload has a partial word")
    return struct.unpack(f"<{len(raw) // 4}I", raw)


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
        "case_id": "HPU_IT_DIR_CMB_009",
        "scheme": "BFV",
        "api": API,
        "producer_commit": producer_commit,
        "poly_modulus_degree": N,
        "component_count": COMPONENTS,
        "q_count": Q_COUNT,
        "domain": "coefficient",
        "instruction_count": EXPECTED_INSTRUCTIONS,
        "dma_count": EXPECTED_DMA,
        "output_lines": OUTPUT_LINES,
        "guard_lines": GUARD_LINES,
    }
    for key, value in expected.items():
        require(metadata.get(key) == value,
                f"HADD metadata {key!r} differs from the fixed CMB009 contract")
    moduli = metadata.get("q_moduli")
    require(isinstance(moduli, list) and len(moduli) == Q_COUNT
            and len(set(moduli)) == Q_COUNT
            and all(isinstance(modulus, int) and 65537 <= modulus <= 0xFFFFFFFF
                    for modulus in moduli),
            "HADD metadata has an invalid Q4 modulus list")
    active = int(metadata.get("active_lines", 0))
    output_offset = int(metadata.get("output_offset", -1))
    guard_offset = int(metadata.get("guard_offset", -1))
    total = int(metadata.get("total_lines", 0))
    require(output_offset + OUTPUT_LINES == active == guard_offset
            and total == active + GUARD_LINES,
            "HADD metadata output/guard geometry is inconsistent")
    require((source / "producer_commit.txt").read_text(encoding="ascii").strip()
            == producer_commit, "HADD producer commit marker differs from metadata")
    return metadata


def validate_allocations(source: Path, metadata: dict[str, object]) -> dict[str, dict[str, str]]:
    allocation_rows = rows(source / "allocations.csv")
    allocations = {entry["id"]: entry for entry in allocation_rows}
    require(len(allocations) == len(allocation_rows), "HADD allocation ids are not unique")
    expected_ids = {"constants/modulus_table"}
    for role in ("left", "right"):
        for component in range(COMPONENTS):
            for basis in range(Q_COUNT):
                expected_ids.add(f"input/{role}/c{component}/mod{basis}")
    for component in range(COMPONENTS):
        for basis in range(Q_COUNT):
            expected_ids.add(f"output/hadd/c{component}/mod{basis}")
    require(set(allocations) == expected_ids,
            "HADD allocation set differs from modulus-table, two inputs and one output")

    table = allocations["constants/modulus_table"]
    require(table["line_offset"] == "0" and table["line_count"] == "1"
            and table["kind"] == "modulus_table" and table["read_only"] == "1",
            "HADD modulus-table allocation is invalid")
    cursor = 1
    for role in ("left", "right"):
        for component in range(COMPONENTS):
            for basis in range(Q_COUNT):
                item = allocations[f"input/{role}/c{component}/mod{basis}"]
                require(int(item["line_offset"]) == cursor
                        and int(item["line_count"]) == POLY_LINES
                        and int(item["word_count"]) == N
                        and item["kind"] == "ciphertext" and item["read_only"] == "1",
                        f"HADD {role} ciphertext allocation is invalid")
                cursor += POLY_LINES
    require(cursor == int(metadata["output_offset"]),
            "HADD output does not follow both inputs")
    for component in range(COMPONENTS):
        for basis in range(Q_COUNT):
            item = allocations[f"output/hadd/c{component}/mod{basis}"]
            require(int(item["line_offset"]) == cursor
                    and int(item["line_count"]) == POLY_LINES
                    and int(item["word_count"]) == N
                    and item["kind"] == "output" and item["read_only"] == "0",
                    "HADD output allocation is invalid")
            cursor += POLY_LINES
    require(cursor == int(metadata["active_lines"]),
            "HADD allocation table does not cover the active image")
    return allocations


def span_words(window: tuple[int, ...], allocation: dict[str, str]) -> tuple[int, ...]:
    first = int(allocation["line_offset"]) * WORDS_PER_LINE
    count = int(allocation["word_count"])
    return window[first:first + count]


def validate_window(source: Path, metadata: dict[str, object],
                    allocations: dict[str, dict[str, str]]) -> tuple[bytes, bytes]:
    window_raw = (source / "window.u32.bin").read_bytes()
    golden_raw = (source / "golden.u32.bin").read_bytes()
    require(len(window_raw) == int(metadata["total_lines"]) * LINE_BYTES,
            "HADD window size differs from metadata")
    require(len(golden_raw) == COMPONENTS * Q_COUNT * N * 4,
            "HADD golden size differs from two Q4 ciphertext components")
    window, golden = words(window_raw), words(golden_raw)
    output_first = int(metadata["output_offset"]) * WORDS_PER_LINE
    output_count = OUTPUT_LINES * WORDS_PER_LINE
    require(all(value == (OUTPUT_POISON ^ index)
                for index, value in enumerate(window[output_first:output_first + output_count])),
            "HADD output was not initialized with the required poison pattern")
    guard_first = int(metadata["guard_offset"]) * WORDS_PER_LINE
    require(all(value == (GUARD_POISON ^ index)
                for index, value in enumerate(window[guard_first:])),
            "HADD tail guard pattern is invalid")

    moduli = metadata["q_moduli"]
    modulus_table = window[:WORDS_PER_LINE]
    for basis, modulus in enumerate(moduli):
        mu = (1 << 64) // modulus
        require(modulus_table[basis * 4:basis * 4 + 4]
                == (modulus, mu & 0xFFFFFFFF, mu >> 32, 0),
                f"HADD modulus-table record {basis} has invalid q/mu")

    golden_index = 0
    for component in range(COMPONENTS):
        for basis, modulus in enumerate(moduli):
            left = span_words(window, allocations[f"input/left/c{component}/mod{basis}"])
            right = span_words(window, allocations[f"input/right/c{component}/mod{basis}"])
            require(len(left) == len(right) == N
                    and all(value < modulus for value in left + right),
                    "HADD input coefficient exceeds its modulus")
            for index in range(N):
                expected = (left[index] + right[index]) % modulus
                require(golden[golden_index] == expected,
                        f"HADD golden differs from modular add at component={component} "
                        f"basis={basis} coefficient={index}")
                golden_index += 1
    return window_raw, golden_raw


def expected_program() -> list[str]:
    result = ["dload x10, x11, p4, 2, 1"]
    for component in range(COMPONENTS):
        for basis in range(Q_COUNT):
            result += [
                f"pmodld {basis}",
                "dload x10, x11, p0, 1, 0",
                "dload x10, x11, p1, 1, 0",
                "padd p2, p0, p1",
                "pfree p0",
                "pfree p1",
                "dstore x10, x11, p2, 1",
            ]
    result += ["pfree p4", "psync"]
    return result


def validate_program(source: Path, encodings: Path,
                     allocations: dict[str, dict[str, str]],
                     metadata: dict[str, object]) -> tuple[list[str], list[int]]:
    program = parse_program(source / "hadd.asm")
    require(program == expected_program() and len(program) == EXPECTED_INSTRUCTIONS,
            "HADD assembly is not the fixed HPU_SEAL BFV Add sequence")
    encoded = [int(bits, 2) for bits in (source / "hadd.inst32").read_text().split()]
    commands = [int(bits, 2) for bits in (source / "hadd.cmd26").read_text().split()]
    require(len(encoded) == len(program)
            and commands == [(word >> 7) | ((1 << 25) if word & 0x7F == 0x2B else 0)
                             for word in encoded],
            "HADD inst32/cmd26 streams are inconsistent")
    check_program(program, encoded, encodings)

    generated_c = (source / "hadd.c").read_text(encoding="utf-8")
    source_words = [int(value, 16)
                    for value in re.findall(r'\.word 0x([0-9A-Fa-f]{8})', generated_c)]
    source_program = [asm for _, asm in re.findall(r'/\* (\d+): (.*?) \*/', generated_c)]
    require(source_words == encoded and source_program == program,
            "HADD generated C differs from ASM/inst32")
    require(f"#define HPU_MEM_LINE_COUNT UINT64_C({metadata['total_lines']})" in generated_c
            and "int hpu_run_hadd(void)" in generated_c,
            "HADD generated C lacks the fixed window or zero-argument wrapper")
    generated_h = (source / "hadd.h").read_text(encoding="utf-8")
    require(f"HPU_PROGRAM_HADD_DMA_COUNT = {EXPECTED_DMA}" in generated_h
            and "int hpu_run_hadd(void);" in generated_h,
            "HADD generated header has the wrong runtime contract")

    resolved = rows(source / "resolved_dma.csv")
    require(len(resolved) == EXPECTED_DMA,
            "HADD resolved DMA schedule has the wrong row count")
    dma_instructions = [index for index, asm in enumerate(program)
                        if asm.startswith(("dload", "dstore"))]
    require(len(dma_instructions) == EXPECTED_DMA,
            "HADD program has the wrong DMA instruction count")
    for dma_index, (entry, instruction_index) in enumerate(zip(resolved, dma_instructions)):
        allocation_id = entry["allocation_id"]
        require(int(entry["dma_index"]) == dma_index
                and int(entry["instruction_index"]) == instruction_index
                and entry["operation_id"] in ("$application", "hadd")
                and entry["normalized_asm"] == program[instruction_index]
                and int(entry["word_hex"], 0) == encoded[instruction_index]
                and allocation_id in allocations,
                f"HADD resolved DMA row {dma_index} differs from the encoded program")
        allocation = allocations[allocation_id]
        require(int(entry["line_offset"]) == int(allocation["line_offset"])
                and int(entry["line_count"]) == int(allocation["line_count"]),
                f"HADD resolved DMA row {dma_index} differs from its allocation")
    return program, encoded


def prepare(source: Path, producer_commit: str, encodings: Path) -> dict[str, object]:
    for relative in REQUIRED_FILES:
        path = source / relative
        require(path.is_file() and path.stat().st_size > 0,
                f"missing or empty HADD producer file: {relative}")
    metadata = validate_metadata(source, producer_commit)
    allocations = validate_allocations(source, metadata)
    window, golden = validate_window(source, metadata, allocations)
    program, encoded = validate_program(source, encodings, allocations, metadata)
    return {"metadata": metadata, "allocations": allocations,
            "window": window, "golden": golden, "program": program, "encoded": encoded}


def publish(source: Path, destination: Path, prepared: dict[str, object],
            producer_commit: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".hadd-data-", dir=destination.parent))
    try:
        for relative in REQUIRED_FILES:
            shutil.copyfile(source / relative, staging / relative)
        metadata = prepared["metadata"]
        moduli = metadata["q_moduli"]
        (staging / "hadd_layout.h").write_text(
            "#ifndef HPU_HADD_LAYOUT_H\n#define HPU_HADD_LAYOUT_H\n\n"
            f"#define HPU_HADD_N {N}U\n"
            f"#define HPU_HADD_COMPONENTS {COMPONENTS}U\n"
            f"#define HPU_HADD_Q_COUNT {Q_COUNT}U\n"
            f"#define HPU_HADD_POLY_LINES {POLY_LINES}U\n"
            f"#define HPU_HADD_ACTIVE_LINES {metadata['active_lines']}U\n"
            f"#define HPU_HADD_OUTPUT_OFFSET {metadata['output_offset']}U\n"
            f"#define HPU_HADD_OUTPUT_LINES {OUTPUT_LINES}U\n"
            f"#define HPU_HADD_GUARD_OFFSET {metadata['guard_offset']}U\n"
            f"#define HPU_HADD_GUARD_LINES {GUARD_LINES}U\n"
            f"#define HPU_HADD_TOTAL_LINES {metadata['total_lines']}U\n"
            f"#define HPU_HADD_INSTRUCTION_COUNT {EXPECTED_INSTRUCTIONS}U\n"
            f"#define HPU_HADD_DMA_COUNT {EXPECTED_DMA}U\n"
            + "".join(f"#define HPU_HADD_Q{index} {modulus}U\n"
                      for index, modulus in enumerate(moduli)) +
            "\n#endif\n", encoding="utf-8")
        (staging / "hadd_delivery.h").write_text(
            "#ifndef HPU_HADD_DELIVERY_H\n#define HPU_HADD_DELIVERY_H\n\n"
            "#include \"hadd.h\"\n#include \"hadd_layout.h\"\n\n"
            "#endif\n", encoding="utf-8")
        (staging / "DELIVERY_SUMMARY.md").write_text(
            "# HPU_SEAL BFV HADD AM import\n\n"
            f"- producer commit: `{producer_commit}`\n"
            f"- API: `{API}`\n- scheme/domain: `BFV/coefficient`\n"
            f"- N/Q/components: `{N}/{Q_COUNT}/{COMPONENTS}`\n"
            f"- q moduli: `{', '.join(str(value) for value in moduli)}`\n"
            f"- instructions/resolved DMA: `{EXPECTED_INSTRUCTIONS}/{EXPECTED_DMA}`\n"
            f"- active/output/guard/total lines: `{metadata['active_lines']}/"
            f"{OUTPUT_LINES}/{GUARD_LINES}/{metadata['total_lines']}`\n"
            f"- window FNV-1a64: `0x{fnv1a64(prepared['window']):016x}`\n"
            f"- golden FNV-1a64: `0x{fnv1a64(prepared['golden']):016x}`\n",
            encoding="utf-8")
        if destination.exists():
            require(destination.is_dir() and not destination.is_symlink()
                    and (destination / "DELIVERY_SUMMARY.md").is_file(),
                    "refusing to replace an unowned HADD delivery directory")
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
    parser.add_argument("--encodings", required=True, type=Path)
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit) is not None,
            "producer commit must be a full lowercase SHA-1")
    source, destination = args.source.resolve(), args.destination.resolve()
    require(source.is_dir() and args.encodings.is_file(),
            "HADD source package or encoder table is missing")
    require(source != destination and source not in destination.parents
            and destination not in source.parents, "HADD source and destination overlap")
    prepared = prepare(source, args.producer_commit, args.encodings)
    publish(source, destination, prepared, args.producer_commit)
    print(f"HPU_SEAL BFV HADD: {EXPECTED_INSTRUCTIONS} instructions, "
          f"{EXPECTED_DMA} resolved DMA, {prepared['metadata']['total_lines']} HPU lines imported")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"HPU_SEAL HADD import failed: {error}") from error
