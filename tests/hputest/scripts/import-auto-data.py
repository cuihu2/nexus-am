#!/usr/bin/env python3
"""Import the producer's complete NTT+Auto package and resolved DMA plan."""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile

from hpu_ntt_layout import bit_reverse
from hpu_opcode_mapping import map_c, map_word
from program_encoding import check_program


_SPEC = importlib.util.spec_from_file_location(
    "import_keyswitch_data", Path(__file__).with_name("import-keyswitch-data.py"))
_COMMON = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
_SPEC.loader.exec_module(_COMMON)

N = _COMMON.N
NUM_Q = _COMMON.NUM_Q
NUM_P = _COMMON.NUM_P
DNUM = _COMMON.DNUM
LINE_BYTES = _COMMON.LINE_BYTES
WORDS_PER_LINE = _COMMON.WORDS_PER_LINE
POLY_LINES = _COMMON.POLY_LINES
EXPECTED_INSTRUCTIONS = 3009
EXPECTED_DMA = 941
GUARD_LINES = 64
GALOIS_ELEMENT = 3


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def rows(path: Path) -> list[dict[str, str]]:
    return _COMMON.rows(path)


def parse_program(package: Path) -> tuple[list[str], list[int], list[dict[str, str]]]:
    program = _COMMON.parse_program(package / "auto.asm")
    require(len(program) == EXPECTED_INSTRUCTIONS and program[-1] == "psync"
            and program.count("psync") == 1,
            "auto: instruction count or terminal PSYNC contract changed")
    words = [int(bits, 2) for bits in (package / "auto.inst32").read_text().split()]
    commands = [int(bits, 2) for bits in (package / "auto.cmd26").read_text().split()]
    source = (package / "auto.c").read_text(encoding="utf-8")
    source_words = [int(word, 16) for word in re.findall(r"\.word 0x([0-9A-Fa-f]{8})", source)]
    source_program = [asm for _, asm in re.findall(r"/\* (\d+): (.*?) \*/", source)]
    require(source_program == program and source_words == words,
            "auto: C comments/words differ from ASM/inst32")
    require(commands == [(word >> 7) | ((1 << 25) if word & 0x7F == 0x2B else 0)
                         for word in words], "auto: cmd26 differs from inst32")
    relocations = rows(package / "dma_relocation_manifest.csv")
    dma_instructions = [index for index, asm in enumerate(program)
                        if asm.startswith(("dload", "dstore"))]
    require(len(relocations) == len(dma_instructions) == EXPECTED_DMA,
            "auto: expected 941 DMA relocations")
    for dma, (entry, instruction) in enumerate(zip(relocations, dma_instructions)):
        operands = program[instruction].replace(",", " ").split()
        direction, object_slot = operands[0], int(operands[3][1:])
        type_or_release = int(operands[4])
        flag = int(operands[5]) if direction == "dload" else 0
        require(int(entry["dma_index"]) == dma
                and int(entry["instruction_index"]) == instruction
                and entry["direction"] == direction
                and int(entry["obj_id"]) == object_slot
                and int(entry["type_or_release"]) == type_or_release
                and int(entry["flag"]) == flag
                and entry["normalized_asm"] == program[instruction]
                and int(entry["word_hex"], 0) == words[instruction]
                and entry["rs1"] == "x10" and entry["rs2"] == "x11",
                f"auto: relocation {dma} differs from encoded program")
    return program, words, relocations


def validate_program(package: Path, encodings: Path):
    program, words, relocations = parse_program(package)
    check_program(program, words, encodings)
    return program, words, relocations


def validate_layout(package: Path) -> None:
    layout = json.loads((package / "test_data/AUTO_LAYOUT.json").read_text(encoding="utf-8"))
    require(layout.get("galois_element") == GALOIS_ELEMENT
            and layout.get("generator3_rotation_step") == 1
            and layout.get("host_preprocess") is False
            and layout.get("inverse_galois_element_mod_2N") == 2731
            and layout.get("inverse_twiddle_profile") == "auto_intt_g3",
            "auto: unsupported Galois/rotation/twiddle profile")
    status = (package / "test_data/STATUS.md").read_text(encoding="utf-8")
    require(status.startswith("PASS:") and "without host permutation" in status,
            "auto: producer did not validate the on-HPU automorphism")


def golden_bytes(package: Path) -> bytes:
    data = package / "test_data"
    entries = {row["path"]: row for row in rows(data / "artifact_manifest.csv")}
    relative = "expected/ciphertext_q.bin"
    require(relative in entries, "auto: final ciphertext golden is absent")
    entry = entries[relative]
    raw = (data / relative).read_bytes()
    require(_COMMON.shape(entry["shape"]) == (2, NUM_Q, N)
            and entry["hardware_visible"] == "0"
            and int(entry["bytes"]) == len(raw)
            and _COMMON.checksum(entry["fnv1a64"]) == _COMMON.fnv1a64(raw),
            "auto: final ciphertext golden manifest/checksum mismatch")
    logical = struct.unpack(f"<{len(raw) // 8}Q", raw)
    require(all(value <= 0xFFFFFFFF for value in logical),
            "auto: golden residue does not fit uint32")
    order = [bit_reverse(index, N) for index in range(N)]
    physical = []
    for base in range(0, len(logical), N):
        physical.extend(logical[base + index] for index in order)
    return struct.pack(f"<{len(physical)}I", *physical)


def prepare(outputs: Path, encodings: Path) -> dict[str, object]:
    package = outputs / "auto"
    _COMMON.validate_params(package, "auto")
    hardware = _COMMON.validate_hardware(package)
    _COMMON.validate_mod_context(hardware)
    validate_layout(package)
    program, words, relocations = validate_program(package, encodings)

    plan = rows(package / "test_data/dma_plan.csv")
    require(len(plan) == len(relocations) == EXPECTED_DMA,
            "auto: resolved plan/relocation counts differ")
    for index, (span, relocation) in enumerate(zip(plan, relocations)):
        require(span["status"] == "RESOLVED"
                and span["instruction_index"] == relocation["instruction_index"]
                and span["dma_index"] == relocation["dma_index"]
                and span["direction"] == relocation["direction"]
                and span["object_slot"] == relocation["obj_id"],
                f"auto: unresolved or mismatched DMA row {index}")
        artifact = span["artifact"]
        require(artifact in hardware["catalog"],
                f"auto: DMA artifact absent from line map: {artifact}")
        base, available = hardware["catalog"][artifact]
        offset, count = int(span["line_offset"]), int(span["line_count"])
        require(count > 0 and base <= offset and offset + count <= base + available,
                f"auto: DMA row {index} exceeds its artifact")

    workspace_path = "images/runtime/auto_workspace.u32.bin"
    require(workspace_path in hardware["catalog"], "auto: workspace image is absent")
    workspace_offset, workspace_lines = hardware["catalog"][workspace_path]
    size_lines = int(hardware["config"]["size_lines"])
    require(workspace_offset + workspace_lines <= size_lines,
            "auto: workspace exceeds the producer window")
    output_rows = sorted(
        (row for row in plan if row["direction"] == "dstore"
         and row["logical_object"].startswith("output.ciphertext_q[")),
        key=lambda row: int(row["logical_object"].split("[")[1].split("]")[0]))
    require(len(output_rows) == 2 * NUM_Q
            and all(int(row["line_count"]) == POLY_LINES for row in output_rows),
            "auto: final output does not contain 2xQ4 polynomials")
    output_offset = int(output_rows[0]["line_offset"])
    require([int(row["line_offset"]) for row in output_rows]
            == [output_offset + index * POLY_LINES for index in range(2 * NUM_Q)]
            and output_offset + 2 * NUM_Q * POLY_LINES
            == workspace_offset + workspace_lines,
            "auto: final output spans are not contiguous at the workspace tail")

    guard = b"".join(struct.pack("<I", (0xA0700000 ^ index * 0x9E3779B1) & 0xFFFFFFFF)
                     for index in range(GUARD_LINES * WORDS_PER_LINE))
    line_rows = [dict(row) for row in hardware["line_rows"]]
    line_rows.append({"path": "am/guard.u32.bin", "role": "AM-owned Auto tail guard",
                      "shape": "", "address_byte": "AM_RELOCATED",
                      "line_offset": str(size_lines), "line_count": str(GUARD_LINES),
                      "payload_words": str(GUARD_LINES * WORDS_PER_LINE),
                      "payload_bytes": str(len(guard)),
                      "padded_words": str(GUARD_LINES * WORDS_PER_LINE),
                      "padded_bytes": str(len(guard))})
    resolved = [{"instruction_index": relocation["instruction_index"],
                 "dma_index": relocation["dma_index"],
                 "direction": span["direction"], "object_slot": span["object_slot"],
                 "logical_object": span["logical_object"], "artifact": span["artifact"],
                 "line_offset": span["line_offset"], "line_count": span["line_count"],
                 "status": span["status"]}
                for span, relocation in zip(plan, relocations)]
    return {
        "package": package, "program": program,
        "words": [map_word(word) for word in words],
        "source": map_c((package / "auto.c").read_text(encoding="utf-8")),
        "resolved": resolved, "line_rows": line_rows,
        "window": hardware["image"] + guard, "window_lines": size_lines,
        "workspace_offset": workspace_offset, "workspace_lines": workspace_lines,
        "output_offset": output_offset, "golden": golden_bytes(package),
    }


def publish(destination: Path, prepared: dict[str, object], commit: str,
            encodings: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".auto-import-", dir=destination.parent))
    try:
        (staging / "window.u32.bin").write_bytes(prepared["window"])
        (staging / "golden.u32.bin").write_bytes(prepared["golden"])
        (staging / "auto.c").write_text(prepared["source"], encoding="utf-8")
        shutil.copyfile(prepared["package"] / "auto.h", staging / "auto.h")
        (staging / "auto.inst32").write_text(
            "".join(f"{word:032b}\n" for word in prepared["words"]), encoding="ascii")
        shutil.copyfile(prepared["package"] / "auto.cmd26", staging / "auto.cmd26")
        fields = ["instruction_index", "dma_index", "direction", "object_slot",
                  "logical_object", "artifact", "line_offset", "line_count", "status"]
        with (staging / "resolved_dma.tsv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
            writer.writeheader(); writer.writerows(prepared["resolved"])
        with (staging / "line_map.tsv").open("w", encoding="utf-8", newline="") as stream:
            fields = list(prepared["line_rows"][0])
            writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
            writer.writeheader(); writer.writerows(prepared["line_rows"])
        layout = ["#ifndef HPU_AUTO_LAYOUT_H", "#define HPU_AUTO_LAYOUT_H",
                  f"#define HPU_AUTO_DMA_COUNT {EXPECTED_DMA}U",
                  f"#define HPU_AUTO_WINDOW_LINES {prepared['window_lines']}U",
                  f"#define HPU_AUTO_GUARD_OFFSET {prepared['window_lines']}U",
                  f"#define HPU_AUTO_GUARD_LINES {GUARD_LINES}U",
                  f"#define HPU_AUTO_TOTAL_LINES {prepared['window_lines'] + GUARD_LINES}U",
                  f"#define HPU_AUTO_WORKSPACE_OFFSET {prepared['workspace_offset']}U",
                  f"#define HPU_AUTO_WORKSPACE_LINES {prepared['workspace_lines']}U",
                  f"#define HPU_AUTO_OUTPUT_OFFSET {prepared['output_offset']}U",
                  "#endif", ""]
        (staging / "auto_layout.h").write_text("\n".join(layout), encoding="utf-8")
        header = ["#ifndef HPU_AUTO_DELIVERY_H", "#define HPU_AUTO_DELIVERY_H",
                  '#include "auto.h"', '#include "auto_layout.h"',
                  "static const hpu_dma_span_t auto_spans[HPU_AUTO_DMA_COUNT] = {"]
        header += [f"    {{{row['line_offset']}U, {row['line_count']}U}}, /* {row['logical_object']} */"
                   for row in prepared["resolved"]]
        header += ["};", "#endif", ""]
        (staging / "auto_delivery.h").write_text("\n".join(header), encoding="utf-8")
        (staging / "producer_commit.txt").write_text(commit + "\n", encoding="ascii")
        shutil.copyfile(encodings, staging / "encoder_words.tsv")
        provenance = staging / "upstream"
        provenance.mkdir()
        for name in ("auto.asm", "auto.c", "auto.h", "auto.inst32", "auto.cmd26",
                     "dma_relocation_manifest.csv"):
            shutil.copyfile(prepared["package"] / name, provenance / name)
        for name in ("params.json", "artifact_manifest.csv", "AUTO_LAYOUT.json", "STATUS.md",
                     "dma_plan.csv", "hardware/abi.json", "hardware/hardware_manifest.csv",
                     "hardware/hpu_mem_config.json", "hardware/line_map.csv",
                     "hardware/mod_ctx_map.csv", "hardware/twiddle_map.csv"):
            target = provenance / name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(prepared["package"] / "test_data" / name, target)
        (staging / "DELIVERY_SUMMARY.md").write_text(
            "# Auto AM import\n\n"
            f"- producer commit: `{commit}`\n- galois element: `{GALOIS_ELEMENT}`\n"
            f"- instructions: {EXPECTED_INSTRUCTIONS}\n- resolved DMA rows: {EXPECTED_DMA}\n"
            f"- HPU window lines: {prepared['window_lines']}\n- guard lines: {GUARD_LINES}\n"
            "- status: semantic NTT+Auto import complete; target qualification pending\n",
            encoding="utf-8")
        if destination.exists():
            marker = destination / "DELIVERY_SUMMARY.md"
            require(destination.is_dir() and not destination.is_symlink() and marker.is_file(),
                    "refusing to replace an unowned Auto import directory")
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
            "producer commit must be a full 40-character Git commit")
    source, destination = args.source.resolve(), args.destination.resolve()
    require(source.is_dir() and args.encodings.is_file(),
            "producer outputs/encoder table is missing")
    require(source != destination and source not in destination.parents
            and destination not in source.parents, "source and destination must not overlap")
    prepared = prepare(source, args.encodings)
    publish(destination, prepared, args.producer_commit, args.encodings)
    print(f"Auto g={GALOIS_ELEMENT}: {EXPECTED_INSTRUCTIONS} instructions, "
          f"{EXPECTED_DMA} resolved DMA, {prepared['window_lines']} HPU lines imported")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, KeyError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"Auto import failed: {error}") from error
