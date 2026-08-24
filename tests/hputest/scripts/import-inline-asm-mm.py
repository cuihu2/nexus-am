#!/usr/bin/env python3
"""Validate and stage the selected inline-asm MM delivery for Nexus-AM."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile


EXPECTED_IMAGES = {
    "images/input_a.u32.bin": (0, 64, 4096, "HPU_MM_LINE_SRC_A"),
    "images/input_b.u32.bin": (64, 64, 4096, "HPU_MM_LINE_SRC_B"),
    "images/expected.u32.bin": (128, 64, 4096, "HPU_MM_LINE_OUTPUT"),
    "constants/mod_ctx.u32.bin": (192, 1, 4, "HPU_MM_LINE_MOD"),
}

EXPECTED_DMA = [
    (0, 0, "dload", 3, 2, 1, "0x00B5272B"),
    (2, 1, "dload", 1, 1, 0, "0x00B5122B"),
    (3, 2, "dload", 2, 1, 0, "0x00B5142B"),
    (7, 3, "dstore", 0, 1, 0, "0x00B5502B"),
]

EXPECTED_MM_WORDS = [
    0x00B5272B,
    0x6000000B,
    0x00B5122B,
    0x00B5142B,
    0x2040800B,
    0x8040000B,
    0x8080000B,
    0x00B5502B,
    0x80C0000B,
    0x7000000B,
]

SELECTED_FILES = [
    "mm.c",
    "mm.h",
    "mm.asm",
    "mm.inst32",
    "mm.cmd26",
    "dma_relocation_manifest.csv",
    "test_data/params.json",
    "test_data/hardware/abi.json",
    "test_data/hardware/line_map.csv",
    "test_data/hardware/hardware_manifest.csv",
    "test_data/hardware/mod_ctx_map.csv",
    "test_data/hardware/images/input_a.u32.bin",
    "test_data/hardware/images/input_a.u32.hex.txt",
    "test_data/hardware/images/input_b.u32.bin",
    "test_data/hardware/images/input_b.u32.hex.txt",
    "test_data/hardware/images/expected.u32.bin",
    "test_data/hardware/images/expected.u32.hex.txt",
    "test_data/hardware/constants/mod_ctx.u32.bin",
    "test_data/hardware/constants/mod_ctx.u32.hex.txt",
]


def fail(message: str) -> None:
    raise RuntimeError(message)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def parse_int(value: str) -> int:
    return int(value, 0)


def fnv1a64(data: bytes) -> int:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def read_u32(path: Path) -> tuple[int, ...]:
    data = path.read_bytes()
    if len(data) % 4 != 0:
        fail(f"{path}: size is not a multiple of uint32")
    return struct.unpack(f"<{len(data) // 4}I", data)


def validate_program(source: Path) -> None:
    manifest = read_csv(source / "dma_relocation_manifest.csv")
    if len(manifest) != len(EXPECTED_DMA):
        fail("MM relocation manifest must contain exactly four DMA rows")
    for row, expected in zip(manifest, EXPECTED_DMA):
        actual = (
            parse_int(row["instruction_index"]),
            parse_int(row["dma_index"]),
            row["direction"],
            parse_int(row["obj_id"]),
            parse_int(row["type_or_release"]),
            parse_int(row["flag"]),
            f"0x{parse_int(row['word_hex']):08X}",
        )
        if actual != expected or row["rs1"] != "x10" or row["rs2"] != "x11":
            fail(f"unexpected MM DMA relocation row: {row}")

    bits = [line.strip() for line in (source / "mm.inst32").read_text().splitlines()
            if line.strip()]
    words = [int(line, 2) for line in bits]
    if words != EXPECTED_MM_WORDS:
        fail("mm.inst32 does not match the reviewed executable MM program")

    generated_c = (source / "mm.c").read_text(encoding="utf-8")
    generated_h = (source / "mm.h").read_text(encoding="utf-8")
    if "HPU_PROGRAM_MM_DMA_COUNT = 4" not in generated_h:
        fail("mm.h does not declare four DMA relocation spans")
    if "int hpu_program_mm(const hpu_dma_span_t *spans" not in generated_c:
        fail("mm.c does not provide the executable hpu_program_mm entry")
    if generated_c.count('__asm__("x10")') != 4 or generated_c.count('__asm__("x11")') != 4:
        fail("mm.c does not bind x10/x11 for every DMA instruction")
    for word in EXPECTED_MM_WORDS:
        if f".word 0x{word:08X}" not in generated_c:
            fail(f"mm.c is missing generated word 0x{word:08X}")
    if "x0, x0" in (source / "mm.asm").read_text(encoding="utf-8"):
        fail("mm.asm contains unresolved x0/x0 DMA operands")


def validate_data(source: Path) -> tuple[int, int, dict[str, dict[str, str]]]:
    params = json.loads((source / "test_data/params.json").read_text())
    if params.get("operation") != "mm" or params.get("N") != 4096:
        fail("MM delivery must be operation=mm and N=4096")
    moduli = params.get("moduli")
    if not isinstance(moduli, list) or len(moduli) != 1 or moduli[0] != 50061313:
        fail("MM delivery must contain the reviewed q=50061313 modulus")
    modulus = int(moduli[0])

    abi = json.loads((source / "test_data/hardware/abi.json").read_text())
    if (abi.get("N"), abi.get("modulus_count"), abi.get("coefficient_bits"),
            abi.get("byte_order"), abi.get("line_bytes"), abi.get("line_words")) != (
                4096, 1, 32, "little-endian", 256, 64):
        fail("MM hardware ABI is not uint32 little-endian / 256-byte lines")

    line_rows = {
        row["path"]: row
        for row in read_csv(source / "test_data/hardware/line_map.csv")
    }
    hardware_rows = {
        row["path"]: row
        for row in read_csv(source / "test_data/hardware/hardware_manifest.csv")
    }
    selected: dict[str, dict[str, str]] = {}
    hardware_root = source / "test_data/hardware"
    for relative, (offset, count, payload_words, _macro) in EXPECTED_IMAGES.items():
        row = line_rows.get(relative)
        hw_row = hardware_rows.get(relative)
        if row is None or hw_row is None:
            fail(f"missing MM line-map/manifest row: {relative}")
        actual = (
            parse_int(row["line_offset"]),
            parse_int(row["line_count"]),
            parse_int(row["payload_words"]),
            parse_int(row["padded_words"]),
        )
        expected_padded = count * 64
        if actual != (offset, count, payload_words, expected_padded):
            fail(f"unexpected MM line-map geometry for {relative}: {actual}")
        binary = hardware_root / relative
        if binary.stat().st_size != expected_padded * 4:
            fail(f"unexpected MM binary size: {binary}")
        manifest_hash = parse_int(hw_row["image_fnv1a64"])
        if fnv1a64(binary.read_bytes()) != manifest_hash:
            fail(f"FNV mismatch for {relative}")
        selected[relative] = hw_row

    input_a = read_u32(hardware_root / "images/input_a.u32.bin")
    input_b = read_u32(hardware_root / "images/input_b.u32.bin")
    expected = read_u32(hardware_root / "images/expected.u32.bin")
    if len(input_a) != 4096 or len(input_b) != 4096 or len(expected) != 4096:
        fail("MM input/expected vector length mismatch")
    for index, (left, right, golden) in enumerate(zip(input_a, input_b, expected)):
        if left >= modulus or right >= modulus or golden != (left * right) % modulus:
            fail(f"MM software golden mismatch at coefficient {index}")

    mod_ctx = read_u32(hardware_root / "constants/mod_ctx.u32.bin")
    mu = ((1 << 64) // modulus) & ((1 << 48) - 1)
    if (mod_ctx[0], mod_ctx[1], mod_ctx[2], mod_ctx[3]) != (
            modulus, mu & 0xFFFFFFFF, mu >> 32, 0):
        fail("MM modulus context does not match q/mu48/reserved layout")
    if any(mod_ctx[4:]):
        fail("MM modulus-context line has nonzero padding")
    return 4096, modulus, selected


def read_encodings(path: Path) -> list[tuple[str, str, str]]:
    rows: list[tuple[str, str, str]] = []
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != "macro_name\tword_hex\tnormalized_asm":
        fail("invalid encoder TSV header")
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != 3:
            fail(f"invalid encoder TSV row: {line}")
        rows.append((fields[0], fields[1], fields[2]))
    if len(rows) != 8:
        fail("encoder TSV must contain the eight Nexus-AM adapter words")
    return rows


def render_header(commit: str, coefficient_count: int, modulus: int,
                  selected: dict[str, dict[str, str]],
                  encodings: list[tuple[str, str, str]]) -> str:
    lines = [
        "#ifndef HPU_INLINE_ASM_MM_DELIVERY_H",
        "#define HPU_INLINE_ASM_MM_DELIVERY_H",
        "",
        "#include <stdint.h>",
        "",
        "/* Generated from the pinned inline-asm encoder and MM manifests. */",
        f'#define HPU_INLINE_ASM_SOURCE_COMMIT "{commit}"',
        f"#define HPU_MM_COEFFICIENTS {coefficient_count}U",
        f"#define HPU_MM_MODULUS UINT32_C({modulus})",
        "#define HPU_MM_REQUIRED_WINDOW_LINES 193U",
        "#define HPU_MM_LINE_BYTES 256U",
        "#define HPU_MM_WORDS_PER_LINE 64U",
    ]
    for relative, (_offset, _count, _payload, macro) in EXPECTED_IMAGES.items():
        row = selected[relative]
        suffix = macro.removeprefix("HPU_MM_LINE_")
        lines.append(f"#define {macro} {parse_int(row['line_offset'])}U")
        lines.append(f"#define HPU_MM_LINES_{suffix} {parse_int(row['line_count'])}U")
        lines.append(
            f"#define HPU_MM_FNV_{suffix} UINT64_C({row['image_fnv1a64']})")
    lines.extend(["", "/* Words below are emitted by the real inline-asm encoder. */"])
    for macro, word, assembly in encodings:
        lines.append(f"/* {assembly} */")
        lines.append(f"#define {macro} UINT32_C({word})")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def sha256_manifest(root: Path) -> str:
    rows = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()
                       and item.name != "SHA256SUMS"):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        rows.append(f"{digest}  {path.relative_to(root).as_posix()}")
    return "\n".join(rows) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--encodings", required=True, type=Path)
    parser.add_argument("--producer-commit", required=True)
    arguments = parser.parse_args()

    source = arguments.source.resolve()
    for relative in SELECTED_FILES:
        if not (source / relative).is_file():
            fail(f"missing selected MM delivery file: {relative}")
    if len(arguments.producer_commit) != 40 or any(
            char not in "0123456789abcdef" for char in arguments.producer_commit):
        fail("producer commit must be a 40-character lowercase SHA")

    validate_program(source)
    coefficient_count, modulus, selected = validate_data(source)
    encodings = read_encodings(arguments.encodings)
    header_text = render_header(arguments.producer_commit, coefficient_count,
                                modulus, selected, encodings)

    destination = arguments.destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".mm-import-", dir=destination.parent))
    backup: Path | None = None
    try:
        for relative in SELECTED_FILES:
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / relative, target)
        (staging / "PRODUCER_COMMIT").write_text(
            arguments.producer_commit + "\n", encoding="utf-8")
        (staging / "encoder_words.tsv").write_text(
            arguments.encodings.read_text(encoding="utf-8"), encoding="utf-8")
        (staging / "inline_asm_mm_delivery.h").write_text(
            header_text, encoding="utf-8")
        (staging / "RESOLVED_DMA_SPANS.csv").write_text(
            "dma_index,role,path,line_offset,line_count\n"
            "0,modulus_context,constants/mod_ctx.u32.bin,192,1\n"
            "1,input_a,images/input_a.u32.bin,0,64\n"
            "2,input_b,images/input_b.u32.bin,64,64\n"
            "3,output,images/expected.u32.bin,128,64\n",
            encoding="utf-8")
        (staging / "DELIVERY_SUMMARY.md").write_text(
            "# Selected inline-asm MM delivery\n\n"
            f"- producer commit: `{arguments.producer_commit}`\n"
            f"- operation: pointwise MM, N={coefficient_count}, q={modulus}\n"
            "- ABI: little-endian uint32, 64 words / 256-byte line\n"
            "- DMA spans: MOD=(192,1), A=(0,64), B=(64,64), OUT=(128,64)\n"
            "- `images/expected.u32.bin` is immutable golden data; Nexus-AM "
            "poisons line 128 before DSTORE and never preloads this golden.\n",
            encoding="utf-8")
        (staging / "SHA256SUMS").write_text(
            sha256_manifest(staging), encoding="utf-8")

        if destination.exists():
            backup = destination.with_name(
                f".{destination.name}.old-{os.getpid()}")
            if backup.exists():
                shutil.rmtree(backup)
            destination.rename(backup)
        staging.rename(destination)
        if backup is not None:
            shutil.rmtree(backup)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        if backup is not None and backup.exists() and not destination.exists():
            backup.rename(destination)
        raise

    arguments.header.parent.mkdir(parents=True, exist_ok=True)
    header_temp = arguments.header.with_name(
        f".{arguments.header.name}.tmp-{os.getpid()}")
    header_temp.write_text(header_text, encoding="utf-8")
    os.replace(header_temp, arguments.header)
    print(f"[hputest] imported validated inline-asm MM delivery: {destination}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError, RuntimeError) as error:
        raise SystemExit(f"ERROR: {error}")
