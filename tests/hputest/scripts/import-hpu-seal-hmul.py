#!/usr/bin/env python3
"""Validate and import the HPU_SEAL BFV HMUL package used by CMB010."""

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
EXPECTED_INSTRUCTIONS = 8025
EXPECTED_DMA = 3177
EXPECTED_ACTIVE_LINES = 32835
EXPECTED_OUTPUT_OFFSET = 32323
EXPECTED_TOTAL_LINES = 32899
OUTPUT_POISON = 0xC0DE1000
GUARD_POISON = 0xA5A51000
API = "hpu::seal_adapter::BfvOperationPlan::append_multiply"

REQUIRED_FILES = (
    "hmul.asm", "hmul.c", "hmul.h", "hmul.inst32", "hmul.cmd26",
    "resolved_dma.csv", "allocations.csv", "metadata.json",
    "producer_commit.txt", "window.u32.bin", "golden.u32.bin",
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
        "case_id": "HPU_IT_DIR_CMB_010",
        "scheme": "BFV",
        "api": API,
        "producer_commit": producer_commit,
        "poly_modulus_degree": N,
        "component_count": COMPONENTS,
        "q_count": Q_COUNT,
        "domain": "coefficient",
        "instruction_count": EXPECTED_INSTRUCTIONS,
        "dma_count": EXPECTED_DMA,
        "active_lines": EXPECTED_ACTIVE_LINES,
        "output_offset": EXPECTED_OUTPUT_OFFSET,
        "output_lines": OUTPUT_LINES,
        "guard_offset": EXPECTED_ACTIVE_LINES,
        "guard_lines": GUARD_LINES,
        "total_lines": EXPECTED_TOTAL_LINES,
    }
    for key, value in expected.items():
        require(metadata.get(key) == value,
                f"HMUL metadata {key!r} differs from the fixed CMB010 contract")
    moduli = metadata.get("q_moduli")
    require(isinstance(moduli, list) and len(moduli) == Q_COUNT
            and len(set(moduli)) == Q_COUNT
            and all(isinstance(modulus, int) and 65537 <= modulus <= 0xFFFFFFFF
                    for modulus in moduli),
            "HMUL metadata has an invalid Q4 modulus list")
    require(isinstance(metadata.get("plain_modulus"), int)
            and 65537 <= int(metadata["plain_modulus"]) <= 0xFFFFFFFF,
            "HMUL metadata has an invalid plaintext modulus")
    for field in ("window_fnv1a64", "golden_fnv1a64"):
        require(re.fullmatch(r"0x[0-9a-f]{16}", str(metadata.get(field))) is not None,
                f"HMUL metadata has an invalid {field}")
    require((source / "producer_commit.txt").read_text(encoding="ascii").strip()
            == producer_commit, "HMUL producer commit marker differs from metadata")
    return metadata


def validate_allocations(source: Path, metadata: dict[str, object]) -> tuple[
        dict[str, dict[str, str]], bytes]:
    allocation_rows = rows(source / "allocations.csv")
    required_columns = {"id", "line_offset", "line_count", "word_count",
                        "kind", "read_only"}
    require(all(set(entry) == required_columns for entry in allocation_rows),
            "HMUL allocation table has invalid columns")
    allocations = {entry["id"]: entry for entry in allocation_rows}
    require(len(allocations) == len(allocation_rows) and allocation_rows,
            "HMUL allocation ids are empty or not unique")

    cursor = 0
    writable = bytearray(int(metadata["total_lines"]))
    kinds = set()
    for entry in sorted(allocation_rows, key=lambda item: int(item["line_offset"])):
        first = int(entry["line_offset"])
        count = int(entry["line_count"])
        count_words = int(entry["word_count"])
        require(first == cursor and count > 0
                and (count - 1) * WORDS_PER_LINE < count_words <= count * WORDS_PER_LINE,
                f"HMUL allocation {entry['id']!r} is not a contiguous valid span")
        require(entry["read_only"] in ("0", "1"),
                f"HMUL allocation {entry['id']!r} has an invalid read_only value")
        kinds.add(entry["kind"])
        if entry["read_only"] == "0":
            writable[first:first + count] = b"\x01" * count
        cursor += count
    require(cursor == int(metadata["active_lines"]),
            "HMUL allocations do not exactly cover the active image")
    require({"modulus_table", "twiddle", "ciphertext", "evaluation_key",
             "constant", "workspace", "output"}.issubset(kinds),
            "HMUL allocations omit a required resource kind")

    table = allocations.get("constants/modulus_table")
    require(table is not None and table["line_offset"] == "0"
            and table["line_count"] == "1" and table["kind"] == "modulus_table"
            and table["read_only"] == "1",
            "HMUL modulus-table allocation is invalid")
    for role in ("left", "right"):
        for component in range(COMPONENTS):
            for basis in range(Q_COUNT):
                identity = f"input/{role}/c{component}/mod{basis}"
                item = allocations.get(identity)
                require(item is not None and int(item["line_count"]) == POLY_LINES
                        and int(item["word_count"]) == N
                        and item["kind"] == "ciphertext" and item["read_only"] == "1",
                        f"HMUL input allocation {identity!r} is invalid")
    output_cursor = int(metadata["output_offset"])
    for component in range(COMPONENTS):
        for basis in range(Q_COUNT):
            identity = f"output/hmul/c{component}/mod{basis}"
            item = allocations.get(identity)
            require(item is not None and int(item["line_offset"]) == output_cursor
                    and int(item["line_count"]) == POLY_LINES
                    and int(item["word_count"]) == N and item["kind"] == "output"
                    and item["read_only"] == "0",
                    f"HMUL output allocation {identity!r} is invalid")
            output_cursor += POLY_LINES
    require(output_cursor == int(metadata["active_lines"]),
            "HMUL output is not the final contiguous active allocation")
    require(all(value == 0 for value in writable[int(metadata["active_lines"]):]),
            "HMUL guard lines were marked writable")
    return allocations, bytes(writable)


def span_words(window: tuple[int, ...], allocation: dict[str, str]) -> tuple[int, ...]:
    first = int(allocation["line_offset"]) * WORDS_PER_LINE
    count = int(allocation["word_count"])
    return window[first:first + count]


def validate_window(source: Path, metadata: dict[str, object],
                    allocations: dict[str, dict[str, str]]) -> tuple[bytes, bytes]:
    window_raw = (source / "window.u32.bin").read_bytes()
    golden_raw = (source / "golden.u32.bin").read_bytes()
    require(len(window_raw) == EXPECTED_TOTAL_LINES * LINE_BYTES,
            "HMUL window size differs from the fixed CMB010 contract")
    require(len(golden_raw) == COMPONENTS * Q_COUNT * N * 4,
            "HMUL golden size differs from two Q4 ciphertext components")
    require(f"0x{fnv1a64(window_raw):016x}" == metadata["window_fnv1a64"],
            "HMUL window checksum differs from metadata")
    require(f"0x{fnv1a64(golden_raw):016x}" == metadata["golden_fnv1a64"],
            "HMUL golden checksum differs from metadata")
    window, golden = words(window_raw), words(golden_raw)
    output_first = EXPECTED_OUTPUT_OFFSET * WORDS_PER_LINE
    output_count = OUTPUT_LINES * WORDS_PER_LINE
    require(all(value == (OUTPUT_POISON ^ index)
                for index, value in enumerate(window[output_first:output_first + output_count])),
            "HMUL output was not initialized with the required poison pattern")
    guard_first = EXPECTED_ACTIVE_LINES * WORDS_PER_LINE
    require(all(value == (GUARD_POISON ^ index)
                for index, value in enumerate(window[guard_first:])),
            "HMUL tail guard pattern is invalid")

    moduli = metadata["q_moduli"]
    modulus_table = window[:WORDS_PER_LINE]
    for basis, modulus in enumerate(moduli):
        mu = (1 << 64) // modulus
        require(modulus_table[basis * 4:basis * 4 + 4]
                == (modulus, mu & 0xFFFFFFFF, mu >> 32, 0),
                f"HMUL modulus-table record {basis} has invalid q/mu")
    for role in ("left", "right"):
        for component in range(COMPONENTS):
            for basis, modulus in enumerate(moduli):
                values = span_words(
                    window, allocations[f"input/{role}/c{component}/mod{basis}"])
                require(len(values) == N and all(value < modulus for value in values),
                        f"HMUL {role} ciphertext coefficient exceeds q{basis}")
    offset = 0
    for _component in range(COMPONENTS):
        for modulus in moduli:
            values = golden[offset:offset + N]
            require(len(values) == N and all(value < modulus for value in values),
                    "HMUL golden coefficient exceeds its output modulus")
            offset += N
    return window_raw, golden_raw


def validate_program(source: Path, encodings: Path,
                     allocations: dict[str, dict[str, str]]) -> tuple[list[str], list[int]]:
    program = parse_program(source / "hmul.asm")
    require(len(program) == EXPECTED_INSTRUCTIONS
            and program[0] == "dload x10, x11, p4, 2, 1"
            and program[-1] == "psync" and program.count("psync") == 1,
            "HMUL assembly does not have the fixed length/table lifetime/terminal PSYNC")
    require(any(item.startswith("pntt ") for item in program)
            and any(item.startswith("pintt ") for item in program)
            and any(item.startswith("pmul ") for item in program)
            and any(item.startswith("pmac ") for item in program),
            "HMUL assembly omits a fused multiply/relinearize phase")
    encoded = [int(bits, 2) for bits in (source / "hmul.inst32").read_text().split()]
    commands = [int(bits, 2) for bits in (source / "hmul.cmd26").read_text().split()]
    require(len(encoded) == len(program)
            and commands == [(word >> 7) | ((1 << 25) if word & 0x7F == 0x2B else 0)
                             for word in encoded],
            "HMUL inst32/cmd26 streams are inconsistent")
    check_program(program, encoded, encodings)

    generated_c = (source / "hmul.c").read_text(encoding="utf-8")
    source_words = [int(value, 16)
                    for value in re.findall(r'\.word 0x([0-9A-Fa-f]{8})', generated_c)]
    source_program = [asm for _, asm in re.findall(r'/\* (\d+): (.*?) \*/', generated_c)]
    require(source_words == encoded and source_program == program,
            "HMUL generated C differs from ASM/inst32")
    require(f"#define HPU_MEM_LINE_COUNT UINT64_C({EXPECTED_TOTAL_LINES})" in generated_c
            and "int hpu_run_hmul(void)" in generated_c,
            "HMUL generated C lacks the fixed window or zero-argument wrapper")
    generated_h = (source / "hmul.h").read_text(encoding="utf-8")
    require(f"HPU_PROGRAM_HMUL_DMA_COUNT = {EXPECTED_DMA}" in generated_h
            and "int hpu_run_hmul(void);" in generated_h,
            "HMUL generated header has the wrong runtime contract")

    resolved = rows(source / "resolved_dma.csv")
    dma_instructions = [index for index, asm in enumerate(program)
                        if asm.startswith(("dload", "dstore"))]
    require(len(resolved) == len(dma_instructions) == EXPECTED_DMA,
            "HMUL resolved DMA schedule has the wrong row count")
    binding_ids = set()
    for dma_index, (entry, instruction_index) in enumerate(zip(resolved, dma_instructions)):
        allocation_id = entry["allocation_id"]
        require(int(entry["dma_index"]) == dma_index
                and int(entry["instruction_index"]) == instruction_index
                and entry["operation_id"] in ("$application", "hmul")
                and entry["normalized_asm"] == program[instruction_index]
                and int(entry["word_hex"], 0) == encoded[instruction_index]
                and allocation_id in allocations,
                f"HMUL resolved DMA row {dma_index} differs from the encoded program")
        allocation = allocations[allocation_id]
        first = int(entry["line_offset"])
        count = int(entry["line_count"])
        allocation_first = int(allocation["line_offset"])
        allocation_count = int(allocation["line_count"])
        require(allocation_first <= first and count > 0
                and first + count <= allocation_first + allocation_count,
                f"HMUL resolved DMA row {dma_index} escapes its allocation")
        if entry["direction"] == "dstore":
            require(allocation["read_only"] == "0",
                    f"HMUL DMA row {dma_index} stores into a read-only allocation")
        binding_ids.add(allocation_id)
    for prefix in ("input/left/", "input/right/", "key/relinearization/top/",
                   "constants/keyswitch/top/", "constants/multiply/top/", "output/hmul/"):
        require(any(identity.startswith(prefix) for identity in binding_ids),
                f"HMUL relocation omits required resource prefix {prefix!r}")
    return program, encoded


def prepare(source: Path, producer_commit: str, encodings: Path) -> dict[str, object]:
    for relative in REQUIRED_FILES:
        path = source / relative
        require(path.is_file() and path.stat().st_size > 0,
                f"missing or empty HMUL producer file: {relative}")
    metadata = validate_metadata(source, producer_commit)
    allocations, writable = validate_allocations(source, metadata)
    window, golden = validate_window(source, metadata, allocations)
    program, encoded = validate_program(source, encodings, allocations)
    return {"metadata": metadata, "allocations": allocations,
            "writable": writable, "window": window, "golden": golden,
            "program": program, "encoded": encoded}


def publish(source: Path, destination: Path, prepared: dict[str, object],
            producer_commit: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".hmul-data-", dir=destination.parent))
    try:
        for relative in REQUIRED_FILES:
            shutil.copyfile(source / relative, staging / relative)
        (staging / "writable_lines.u8.bin").write_bytes(prepared["writable"])
        metadata = prepared["metadata"]
        moduli = metadata["q_moduli"]
        (staging / "hmul_layout.h").write_text(
            "#ifndef HPU_HMUL_LAYOUT_H\n#define HPU_HMUL_LAYOUT_H\n\n"
            f"#define HPU_HMUL_N {N}U\n"
            f"#define HPU_HMUL_COMPONENTS {COMPONENTS}U\n"
            f"#define HPU_HMUL_Q_COUNT {Q_COUNT}U\n"
            f"#define HPU_HMUL_POLY_LINES {POLY_LINES}U\n"
            f"#define HPU_HMUL_ACTIVE_LINES {EXPECTED_ACTIVE_LINES}U\n"
            f"#define HPU_HMUL_OUTPUT_OFFSET {EXPECTED_OUTPUT_OFFSET}U\n"
            f"#define HPU_HMUL_OUTPUT_LINES {OUTPUT_LINES}U\n"
            f"#define HPU_HMUL_GUARD_OFFSET {EXPECTED_ACTIVE_LINES}U\n"
            f"#define HPU_HMUL_GUARD_LINES {GUARD_LINES}U\n"
            f"#define HPU_HMUL_TOTAL_LINES {EXPECTED_TOTAL_LINES}U\n"
            f"#define HPU_HMUL_INSTRUCTION_COUNT {EXPECTED_INSTRUCTIONS}U\n"
            f"#define HPU_HMUL_DMA_COUNT {EXPECTED_DMA}U\n"
            + "".join(f"#define HPU_HMUL_Q{index} {modulus}U\n"
                      for index, modulus in enumerate(moduli))
            + "\n#endif\n", encoding="utf-8")
        (staging / "hmul_delivery.h").write_text(
            "#ifndef HPU_HMUL_DELIVERY_H\n#define HPU_HMUL_DELIVERY_H\n\n"
            "#include \"hmul.h\"\n#include \"hmul_layout.h\"\n\n"
            "#endif\n", encoding="utf-8")
        (staging / "DELIVERY_SUMMARY.md").write_text(
            "# HPU_SEAL BFV HMUL AM import\n\n"
            f"- producer commit: `{producer_commit}`\n"
            f"- API: `{API}`\n- scheme/domain: `BFV/coefficient`\n"
            f"- N/Q/components: `{N}/{Q_COUNT}/{COMPONENTS}`\n"
            f"- q moduli: `{', '.join(str(value) for value in moduli)}`\n"
            f"- instructions/resolved DMA: `{EXPECTED_INSTRUCTIONS}/{EXPECTED_DMA}`\n"
            f"- active/output/guard/total lines: `{EXPECTED_ACTIVE_LINES}/"
            f"{OUTPUT_LINES}/{GUARD_LINES}/{EXPECTED_TOTAL_LINES}`\n"
            f"- window FNV-1a64: `0x{fnv1a64(prepared['window']):016x}`\n"
            f"- golden FNV-1a64: `0x{fnv1a64(prepared['golden']):016x}`\n",
            encoding="utf-8")
        if destination.exists():
            require(destination.is_dir() and not destination.is_symlink()
                    and (destination / "DELIVERY_SUMMARY.md").is_file(),
                    "refusing to replace an unowned HMUL delivery directory")
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
            "HMUL source package or encoder table is missing")
    require(source != destination and source not in destination.parents
            and destination not in source.parents, "HMUL source and destination overlap")
    prepared = prepare(source, args.producer_commit, args.encodings)
    publish(source, destination, prepared, args.producer_commit)
    print(f"HPU_SEAL BFV HMUL: {EXPECTED_INSTRUCTIONS} instructions, "
          f"{EXPECTED_DMA} resolved DMA, {EXPECTED_TOTAL_LINES} HPU lines imported")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"HPU_SEAL HMUL import failed: {error}") from error
