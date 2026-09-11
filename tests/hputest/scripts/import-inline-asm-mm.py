#!/usr/bin/env python3
"""Validate and stage the selected inline-asm MM delivery for Nexus-AM."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
from pathlib import Path
import shutil
import struct
import tempfile

from hpu_opcode_mapping import DMA_OPCODE, TARGET_OPCODE, map_c, map_word
from hpu_ntt_layout import forward_layout


EXPECTED_IMAGES = {
    "images/input_a.u32.bin": (0, 64, 4096, "HPU_MM_LINE_SRC_A"),
    "images/input_b.u32.bin": (64, 64, 4096, "HPU_MM_LINE_SRC_B"),
    "images/expected.u32.bin": (128, 64, 4096, "HPU_MM_LINE_OUTPUT"),
    "constants/mod_ctx.u32.bin": (192, 1, 4, "HPU_MM_LINE_MOD"),
}

EXPECTED_DMA = [
    (0, 0, "dload", 3, 2, 1, "0x06B540AB"),
    (2, 1, "dload", 1, 1, 0, "0x02B5202B"),
    (3, 2, "dload", 2, 1, 0, "0x04B5202B"),
    (7, 3, "dstore", 0, 1, 0, "0x00B5502B"),
]

EXPECTED_MM_WORDS = [
    0x06B540AB,
    0x6000005B,
    0x02B5202B,
    0x04B5202B,
    0x2040805B,
    0x8040005B,
    0x8080005B,
    0x00B5502B,
    0x80C0005B,
    0x7000005B,
]

BASE_SELECTED_FILES = [
    "mm.c",
    "mm.h",
    "mm.asm",
    "mm.inst32",
    "mm.cmd26",
    "dma_relocation_manifest.csv",
    "test_data/params.json",
    "test_data/input_a.bin",
    "test_data/input_b.bin",
    "test_data/expected.bin",
    "test_data/hardware/abi.json",
    "test_data/hardware/line_map.csv",
    "test_data/hardware/hardware_manifest.csv",
    "test_data/hardware/mod_ctx_map.csv",
    "test_data/hardware/images/input_a.u32.bin",
    "test_data/hardware/images/input_b.u32.bin",
    "test_data/hardware/images/expected.u32.bin",
    "test_data/hardware/constants/mod_ctx.u32.bin",
]

UPSTREAM_PROGRAM_FILES = (
    "mm.c", "mm.h", "mm.asm", "mm.inst32", "mm.cmd26",
    "dma_relocation_manifest.csv",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def selected_files(source: Path) -> list[str]:
    """Resolve human-readable companions from the producer manifest."""
    manifest_path = source / "test_data/hardware/hardware_manifest.csv"
    manifest_rows = {row["path"]: row for row in read_csv(manifest_path)}
    hardware_root = (source / "test_data/hardware").resolve(strict=True)
    selected = list(BASE_SELECTED_FILES)

    for binary_path in EXPECTED_IMAGES:
        row = manifest_rows.get(binary_path)
        if row is None:
            fail(f"missing hardware manifest row: {binary_path}")
        readable_path = row.get("readable_path", "").strip()
        relative = Path(readable_path)
        if (not readable_path or relative.is_absolute() or
                ".." in relative.parts):
            fail(f"invalid readable_path for {binary_path}: {readable_path!r}")
        candidate = (hardware_root / relative).resolve(strict=True)
        try:
            candidate.relative_to(hardware_root)
        except ValueError:
            fail(f"readable_path escapes hardware package: {readable_path}")
        selected.append(f"test_data/hardware/{relative.as_posix()}")

    if len(selected) != len(set(selected)):
        fail("selected MM delivery contains duplicate paths")
    return selected


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

    # 当前 main 使用标准 GPR 位段；custom1 仍直通 payload 并置 kind。
    # 必须保留 rs1/rs2 编号，不能接受旧版重排/丢弃操作数的 cmd26。
    commands = [int(line, 2) for line in
                (source / "mm.cmd26").read_text().splitlines() if line.strip()]
    expected_commands = [(word >> 7) | (1 << 25 if word & 0x7F == 0x2B else 0)
                         for word in EXPECTED_MM_WORDS]
    if commands != expected_commands:
        fail("mm.cmd26 does not preserve the reviewed instruction payload")

    generated_c = (source / "mm.c").read_text(encoding="utf-8")
    generated_h = (source / "mm.h").read_text(encoding="utf-8")
    if "HPU_PROGRAM_MM_DMA_COUNT = 4" not in generated_h:
        fail("mm.h does not declare four DMA relocation spans")
    if "int hpu_program_mm(const hpu_dma_span_t *spans" not in generated_c:
        fail("mm.c does not provide the executable hpu_program_mm entry")
    if generated_c.count('__asm__("x10")') != 4 or generated_c.count('__asm__("x11")') != 4:
        fail("mm.c does not bind x10/x11 for every DMA instruction")
    c_words = [int(word, 16) for word in
               re.findall(r"\.word 0x([0-9a-fA-F]{8})", generated_c)]
    if c_words != EXPECTED_MM_WORDS:
        fail("mm.c does not preserve the complete reviewed instruction stream")
    if "spans[3].line_count != hpu_obj_len[0]" not in generated_c:
        fail("mm.c is missing the DSTORE span/OBJ.len guard")
    if "x0, x0" in (source / "mm.asm").read_text(encoding="utf-8"):
        fail("mm.asm contains unresolved x0/x0 DMA operands")


def validate_data(source: Path) -> tuple[int, int, dict[str, dict[str, str]]]:
    params = json.loads((source / "test_data/params.json").read_text())
    if params.get("operation") != "mm" or params.get("N") != 4096:
        fail("MM delivery must be operation=mm and N=4096")
    if (params.get("input_domain"), params.get("output_domain")) != ("NTT", "NTT") or \
            "NTT domain P-network physical" not in params.get("hardware_layout", ""):
        fail("MM delivery must declare the current P-network NTT layout")
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
    # MM 属于NTT域：硬件数组使用forward_layout，不是系数域的bit_reverse。
    layout = forward_layout(4096)
    for name, hardware_values in (("input_a", input_a), ("input_b", input_b),
                                  ("expected", expected)):
        raw = (source / "test_data" / f"{name}.bin").read_bytes()
        if len(raw) != 4096 * 8:
            fail(f"MM {name}: expected 4096 natural-order uint64 values")
        logical = struct.unpack("<4096Q", raw)
        for physical, value in enumerate(hardware_values):
            logical_index = layout[physical]
            if value != logical[logical_index]:
                fail(f"MM {name}: physical/logical mapping mismatch at {physical}")
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
    if len(rows) < 8:
        fail("encoder TSV does not contain the required Nexus-AM adapters")
    macro_names = [row[0] for row in rows]
    if len(set(macro_names)) != len(macro_names):
        fail("encoder TSV contains duplicate macro names")
    return rows


def map_encodings(rows: list[tuple[str, str, str]]) -> list[tuple[str, str, str]]:
    return [(name, f"0x{map_word(parse_int(word)):08X}", assembly)
            for name, word, assembly in rows]


def render_encodings(rows: list[tuple[str, str, str]]) -> str:
    return "macro_name\tword_hex\tnormalized_asm\n" + "".join(
        f"{name}\t{word}\t{assembly}\n" for name, word, assembly in rows)


def opcode_map_rows() -> list[dict[str, str]]:
    rows = []
    for index, word in enumerate(EXPECTED_MM_WORDS):
        kind = int(word & 0x7F == DMA_OPCODE)
        rows.append({
            "instruction_index": str(index),
            "source_word": f"0x{word:08X}",
            "target_word": f"0x{map_word(word):08X}",
            "cmd_kind": str(kind),
            "cmd26": f"0x{((kind << 25) | (word >> 7)):07X}",
        })
    return rows


def validate_mapped_delivery(root: Path) -> None:
    """保留完整上游证据，新主线的计算/控制及DMA指令必须原样接收。"""
    upstream = root / "upstream"
    validate_program(upstream)
    expected_source = map_c((upstream / "mm.c").read_text(encoding="utf-8"))
    if (root / "mm.c").read_text(encoding="utf-8") != expected_source:
        fail("mapped mm.c changed more than opcode, including possible OBJ.len guard loss")
    words = [int(line, 2) for line in (root / "mm.inst32").read_text().splitlines()
             if line.strip()]
    if words != [map_word(word) for word in EXPECTED_MM_WORDS]:
        fail("mapped mm.inst32 does not preserve the upstream payload")
    # cmd26 不变，DMA relocation/GPR、C 接口和助记符来源同样原样保留。
    for name in ("mm.cmd26", "mm.h", "mm.asm", "dma_relocation_manifest.csv"):
        if (root / name).read_bytes() != (upstream / name).read_bytes():
            fail(f"opcode mapping unexpectedly changed {name}")
    mapped = map_encodings(read_encodings(upstream / "encoder_words.tsv"))
    if read_encodings(root / "encoder_words.tsv") != mapped:
        fail("mapped encoder_words.tsv does not preserve upstream payloads")
    header = (root / "inline_asm_mm_delivery.h").read_text(encoding="utf-8")
    definitions = re.findall(r"^#define (HPU_INSN_\w+) UINT32_C\((0x[0-9A-Fa-f]{8})\)$",
                             header, re.MULTILINE)
    if definitions != [(name, word) for name, word, _assembly in mapped]:
        fail("generated header does not match mapped encoder words")
    if read_csv(root / "opcode_map.csv") != opcode_map_rows():
        fail("opcode_map.csv does not match the source/target/cmd26 contract")


def render_header(commit: str, coefficient_count: int, modulus: int,
                  selected: dict[str, dict[str, str]],
                  encodings: list[tuple[str, str, str]]) -> str:
    lines = [
        "#ifndef HPU_INLINE_ASM_MM_DELIVERY_H",
        "#define HPU_INLINE_ASM_MM_DELIVERY_H",
        "",
        "#include <stdint.h>",
        "",
        "/* Native inline-asm custom-2/custom-1 words; AM validates without rewriting. */",
        f'#define HPU_INLINE_ASM_SOURCE_COMMIT "{commit}"',
        f"#define HPU_INLINE_ASM_ARITH_OPCODE UINT32_C(0x{TARGET_OPCODE:02X})",
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
    lines.extend(["", "/* Encoder payloads unchanged; control/compute use custom-2, DMA stays custom-1. */"])
    for macro, word, assembly in encodings:
        lines.append(f"/* {assembly} */")
        lines.append(f"#define {macro} UINT32_C({word})")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--encodings", required=True, type=Path)
    parser.add_argument("--producer-commit", required=True)
    arguments = parser.parse_args()

    source = arguments.source.resolve()
    delivery_files = selected_files(source)
    for relative in delivery_files:
        if not (source / relative).is_file():
            fail(f"missing selected MM delivery file: {relative}")
    if len(arguments.producer_commit) != 40 or any(
            char not in "0123456789abcdef" for char in arguments.producer_commit):
        fail("producer commit must be a 40-character lowercase Git object ID")

    validate_program(source)
    coefficient_count, modulus, selected = validate_data(source)
    encodings = map_encodings(read_encodings(arguments.encodings))
    header_text = render_header(arguments.producer_commit, coefficient_count,
                                modulus, selected, encodings)

    destination = arguments.destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".mm-import-", dir=destination.parent))
    backup: Path | None = None
    try:
        for relative in delivery_files:
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / relative, target)
        upstream = staging / "upstream"
        upstream.mkdir()
        for name in UPSTREAM_PROGRAM_FILES:
            shutil.copy2(source / name, upstream / name)
        shutil.copy2(arguments.encodings, upstream / "encoder_words.tsv")
        (staging / "mm.c").write_text(
            map_c((source / "mm.c").read_text(encoding="utf-8")), encoding="utf-8")
        (staging / "mm.inst32").write_text(
            "".join(f"{map_word(word):032b}\n" for word in EXPECTED_MM_WORDS),
            encoding="utf-8")
        with (staging / "opcode_map.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=(
                "instruction_index", "source_word", "target_word", "cmd_kind", "cmd26"))
            writer.writeheader()
            writer.writerows(opcode_map_rows())
        (staging / "PRODUCER_COMMIT").write_text(
            arguments.producer_commit + "\n", encoding="utf-8")
        (staging / "encoder_words.tsv").write_text(
            render_encodings(encodings), encoding="utf-8")
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
            "- NTT physical order: memory[p] = logical_ntt[forward_layout(p)]; "
            "original natural-order uint64 vectors are retained in test_data/.\n"
            "- DMA spans: MOD=(192,1), A=(0,64), B=(64,64), OUT=(128,64)\n"
            "- `images/expected.u32.bin` is immutable golden data; Nexus-AM "
            "poisons line 128 before DSTORE and never preloads this golden.\n"
            "- `upstream/` preserves original inline-asm program/encoder files. "
            "Upstream natively emits 0x5B; AM preserves mm.c/inst32 and encoder "
            "words byte-for-byte. opcode_map.csv records identity mapping. DMA 0x2B, "
            "inst[31:7], cmd26, x10/x11 and DSTORE OBJ.len guards are unchanged.\n"
            "- The program retains one terminal PSYNC (0x7000005B), no internal barrier. "
            "CPU frontend must route custom-2 (0x5B) to HPU cmd_kind=0.\n",
            encoding="utf-8")
        validate_mapped_delivery(staging)
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
