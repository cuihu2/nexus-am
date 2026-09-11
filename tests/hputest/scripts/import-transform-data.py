#!/usr/bin/env python3
"""为 producer 整体 NTT/INTT 绑定已验证的 16 次 DMA，不重写算法指令序列。"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import struct

from hpu_opcode_mapping import map_c, map_word
from program_encoding import check_program
from hpu_ntt_layout import (CONVENTION, HARDWARE_LAYOUT, bit_reverse,
                            expected_twiddles, physical_words, stage_reference)

N, Q, LINE_BYTES, WINDOW_LINES = 4096, 50061313, 256, 640
INPUT, OUTPUT, MOD, FACTOR, STAGES = 1, 65, 129, 130, 194


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def one(rows: list[dict[str, str]], **fields: str) -> dict[str, str]:
    found = [row for row in rows if all(row.get(k) == v for k, v in fields.items())]
    require(len(found) == 1, f"missing or duplicate manifest row: {fields}")
    return found[0]


def mathematical_transform(data: list[int], psi: int, inverse: bool) -> list[int]:
    """独立数学复核 producer golden；bit-reversal 仅为 CPU DIT 实现，不改 DDR 数据。"""
    values = list(data)
    if not inverse:
        values = [value * pow(psi, i, Q) % Q for i, value in enumerate(values)]
    for i in range(N):
        j = int(f"{i:012b}"[::-1], 2)
        if i < j:
            values[i], values[j] = values[j], values[i]
    omega = psi * psi % Q
    if inverse:
        omega = pow(omega, Q - 2, Q)
    for stage in range(12):
        length = 1 << (stage + 1)
        step = pow(omega, N // length, Q)
        for begin in range(0, N, length):
            factor = 1
            for j in range(length // 2):
                even, odd = begin + j, begin + j + length // 2
                a, b = values[even], values[odd] * factor % Q
                values[even], values[odd] = (a + b) % Q, (a - b) % Q
                factor = factor * step % Q
    if inverse:
        scale = pow(N, Q - 2, Q)
        inv_psi = pow(psi, Q - 2, Q)
        values = [value * scale % Q * pow(inv_psi, i, Q) % Q
                  for i, value in enumerate(values)]
    return values


def expected_program(direction: str) -> tuple[list[str], list[str]]:
    program = ["dload x10, x11, p2, 2, 1", "pmodld 0", "dload x10, x11, p0, 1, 0"]
    assets = ["constants/mod_ctx.u32.bin", "images/input.u32.bin"]
    if direction == "ntt":
        program += ["dload x10, x11, p1, 1, 0", "pmul p0, p0, p1", "pfree p1"]
        assets.append("constants/twiddle/ntt/basis_00/pre_twist.u32.bin")
    for stage in range(12):
        src, dst = (0, 3) if stage % 2 == 0 else (3, 0)
        program += ["dload x10, x11, p1, 1, 0", f"p{direction} p{dst}, p{src}, p1, {stage}, 0, 0",
                    f"pfree p{src}", "pfree p1"]
        assets.append(f"constants/twiddle/{direction}/basis_00/stage_{stage:02d}.u32.bin")
    if direction == "intt":
        program += ["dload x10, x11, p1, 1, 0", "pmul p0, p0, p1", "pfree p1"]
        assets.append("constants/twiddle/intt/basis_00/post_untwist_scale.u32.bin")
    program += ["dstore x10, x11, p0, 1", "pfree p2", "psync"]
    assets.append("am/output-poison")
    return program, assets


def validate_package(source: Path, direction: str, encodings: Path) -> dict[str, object]:
    package = source / direction
    data = package / "test_data"
    hardware = data / "hardware"
    params = json.loads((data / "params.json").read_text(encoding="utf-8"))
    abi = json.loads((hardware / "abi.json").read_text(encoding="utf-8"))
    require(params.get("N") == N and params.get("moduli") == [Q] and
            params.get("operation") == direction, f"{direction}: expected N4096/Q0 producer package")
    require(HARDWARE_LAYOUT in params.get("hardware_layout", ""),
            f"{direction}: obsolete coefficient/NTT physical layout")
    require(abi.get("coefficient_bits") == 32 and abi.get("line_bytes") == LINE_BYTES and
            abi.get("byte_order") == "little-endian", f"{direction}: incompatible hardware ABI")
    require(abi.get("twiddle", {}).get("convention") == CONVENTION, "unsupported twiddle convention")
    line_map = read_rows(hardware / "line_map.csv")
    twiddles = read_rows(hardware / "twiddle_map.csv")
    full_image = (hardware / "hpu_mem_image.u32.bin").read_bytes()

    def load(path: str, count: int) -> bytes:
        geometry = one(line_map, path=path)
        blob = (hardware / path).read_bytes()
        require(int(geometry["line_count"]) == count and len(blob) == count * LINE_BYTES and
                int(geometry["padded_bytes"]) == len(blob), f"{direction}/{path}: geometry mismatch")
        offset = int(geometry["line_offset"]) * LINE_BYTES
        require(full_image[offset:offset + len(blob)] == blob, f"{path}: differs from unified image")
        return blob

    input_blob = load("images/input.u32.bin", 64)
    golden_blob = load("images/expected.u32.bin", 64)
    inputs = list(struct.unpack("<4096I", input_blob))
    golden = list(struct.unpack("<4096I", golden_blob))
    require(all(value < Q for value in inputs + golden), "noncanonical input/golden")
    logical = {}
    for name, values in (("input", inputs), ("expected", golden)):
        math_blob = (data / f"{name}.bin").read_bytes()
        require(len(math_blob) == N * 8, f"{direction}/{name}: wrong mathematical data size")
        logical[name] = list(struct.unpack("<4096Q", math_blob))
        ntt_domain = (direction == "intt") == (name == "input")
        require(physical_words(logical[name], ntt_domain) == values,
                f"{direction}/{name}: hardware image differs from specified physical permutation")
    pre = one(twiddles, direction="ntt", basis_index="0", phase="pre_twist", stage="-1")
    pre_words = list(struct.unpack("<4096I", load(pre["path"], 64)))
    psi = pre_words[N // 2]
    require(pow(psi, N, Q) == Q - 1 and pow(psi, 2 * N, Q) == 1, "invalid primitive root")
    require(pre_words == [pow(psi, bit_reverse(i, N), Q) for i in range(N)],
            "pre_twist differs from bit-reversed coefficient powers")
    require(mathematical_transform(logical["input"], psi, direction == "intt") == logical["expected"],
            f"{direction}: mathematical reference differs from producer golden")
    tables = expected_twiddles(N, Q, psi)

    program, paths = expected_program(direction)
    c_source = (package / f"{direction}.c").read_text(encoding="utf-8")
    comments = re.findall(r"/\* (\d+): (.*?) \*/", c_source)
    require(comments == [(str(i), asm) for i, asm in enumerate(program)],
            f"{direction}: producer program changed; DMA bindings need review")
    words = [int(bits, 2) for bits in (package / f"{direction}.inst32").read_text().split()]
    c_words = [int(word, 16) for word in re.findall(r"\.word 0x([0-9a-fA-F]{8})", c_source)]
    require(words == c_words and len(words) == len(program), f"{direction}: C/inst32 mismatch")
    check_program(program, words, encodings)
    commands = [int(bits, 2) for bits in (package / f"{direction}.cmd26").read_text().split()]
    expected_commands = [(word >> 7) | ((1 << 25) if word & 0x7f == 0x2b else 0) for word in words]
    require(commands == expected_commands, f"{direction}: cmd26 mismatch")
    require(c_source.count('__asm__("x10")') == 16 and c_source.count('__asm__("x11")') == 16,
            f"{direction}: DMA source-register binding changed")
    relocations = read_rows(package / "dma_relocation_manifest.csv")
    dma_instructions = [i for i, asm in enumerate(program) if asm.startswith(("dload", "dstore"))]
    require(len(relocations) == len(paths) == len(dma_instructions) == 16, "expected exactly 16 DMAs")
    image = [((0xA5390000 ^ (i * 0x45D9F3B)) & 0xffffffff) for i in range(WINDOW_LINES * 64)]
    for i in range(OUTPUT * 64, OUTPUT * 64 + N):
        image[i] = 0xDEADBEEF
    bindings = []
    # INTT 也使用前向 pre-twist 表提取 primitive root，必须保留这一验证依赖。
    selected_files = [pre["path"]] if direction == "intt" else []
    physical_result = list(inputs)
    for index, (relocation, path, instruction) in enumerate(zip(relocations, paths, dma_instructions)):
        asm = program[instruction]
        require(int(relocation["dma_index"]) == index and
                int(relocation["instruction_index"]) == instruction and
                relocation["normalized_asm"] == asm and int(relocation["word_hex"], 0) == words[instruction] and
                relocation["rs1"] == "x10" and relocation["rs2"] == "x11", "DMA relocation mismatch")
        if path == "am/output-poison":
            line, count = OUTPUT, 64
        elif path == "constants/mod_ctx.u32.bin":
            line, count = MOD, 1
        elif path == "images/input.u32.bin":
            line, count = INPUT, 64
        elif path.endswith(("pre_twist.u32.bin", "post_untwist_scale.u32.bin")):
            line, count = FACTOR, 64
        else:
            stage = int(re.search(r"stage_(\d+)\.u32\.bin$", path).group(1))
            line, count = STAGES + stage * 32, 32
        if path != "am/output-poison":
            blob = load(path, count)
            values = list(struct.unpack(f"<{len(blob) // 4}I", blob))
            if line == MOD:
                mu = (1 << 64) // Q
                require(values[:4] == [Q, mu & 0xffffffff, mu >> 32, 0] and not any(values[4:]),
                        "mod context differs from q32/mu48 reference")
            elif line == FACTOR:
                expected = ([pow(psi, bit_reverse(i, N), Q) for i in range(N)] if direction == "ntt" else
                            [pow(N, Q - 2, Q) * pow(pow(psi, Q - 2, Q), bit_reverse(i, N), Q) % Q
                             for i in range(N)])
                require(values == expected, f"{direction}: pre/post factor mismatch")
                physical_result = [a * b % Q for a, b in zip(physical_result, values)]
            elif line >= STAGES:
                stage = (line - STAGES) // 32
                forward_stage = stage if direction == "ntt" else 11 - stage
                table_row = one(twiddles, direction=direction, basis_index="0", phase="butterfly", stage=str(stage))
                require(table_row["path"] == path and int(table_row["forward_stage"]) == forward_stage and
                        int(table_row["batch_count"]) == 32 and int(table_row["lanes_per_batch"]) == 64 and
                        table_row["loader_mode"] == ("sequential_128" if forward_stage < 7 else "interleaved_64x2") and
                        int(table_row["value_count"]) == N // 2 and int(table_row["line_count"]) == 32,
                        f"{direction}: stage {stage} metadata mismatch")
                require(values == tables[direction][stage],
                        f"{direction}: stage {stage} twiddle differs from physical lane/scale formula")
                physical_result = stage_reference(physical_result, values, Q, stage, direction == "intt")
            image[line * 64:(line + count) * 64] = values
            selected_files.append(path)
        bindings.append(dict(dma=index, instruction=instruction, operation=asm,
                             artifact=path, line=line, lines=count))
    require(physical_result == golden, f"{direction}: physical stage model differs from mathematical golden")
    return dict(image=struct.pack(f"<{len(image)}I", *image), golden=golden_blob,
                bindings=bindings, selected_files=selected_files, c_source=map_c(c_source),
                mapped_words=[map_word(word) for word in words])


def write_package(source: Path, destination: Path, direction: str, prepared: dict[str, object]) -> None:
    output = destination / direction
    output.mkdir(parents=True, exist_ok=True)
    (output / "window.u32.bin").write_bytes(prepared["image"])
    (output / "golden.u32.bin").write_bytes(prepared["golden"])
    (output / f"{direction}.c").write_text(prepared["c_source"], encoding="utf-8")
    shutil.copyfile(source / direction / f"{direction}.h", output / f"{direction}.h")
    (output / f"{direction}.inst32").write_text(
        "".join(f"{word:032b}\n" for word in prepared["mapped_words"]), encoding="ascii")
    header = [f'#include "{direction}.h"', '#include <hpu/transform.h>',
              f'extern const uint32_t transform_{direction}_image[TRANSFORM_WORDS];',
              f'extern const uint32_t transform_{direction}_golden[TRANSFORM_N];',
              f'static const hpu_dma_span_t transform_{direction}_spans[16] = {{']
    for row in prepared["bindings"]:
        header.append(f'    {{{row["line"]}U, {row["lines"]}U}}, /* {row["artifact"]} */')
    header += ['};', f'static const struct transform_binding transform_{direction}_bindings[16] = {{']
    for row in prepared["bindings"]:
        header.append(f'    {{{row["instruction"]}U, {row["line"]}U, {row["lines"]}U, '
                      f'"{row["operation"]}", "{row["artifact"]}"}},')
    header += ['};', '']
    (output / "delivery.h").write_text("\n".join(header), encoding="utf-8")
    with (output / "resolved_dma.tsv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(prepared["bindings"][0]), delimiter="\t")
        writer.writeheader()
        writer.writerows(prepared["bindings"])
    provenance = output / "upstream"
    provenance.mkdir(exist_ok=True)
    names = [f"{direction}.{extension}" for extension in ("c", "h", "asm", "inst32", "cmd26")]
    names += ["dma_relocation_manifest.csv", "test_data/params.json", "test_data/input.bin", "test_data/expected.bin",
              "test_data/hardware/abi.json", "test_data/hardware/line_map.csv", "test_data/hardware/twiddle_map.csv"]
    names += [f"test_data/hardware/{name}" for name in prepared["selected_files"]]
    names.append("test_data/hardware/images/expected.u32.bin")
    for name in names:
        copied = provenance / name
        copied.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / direction / name, copied)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--producer-commit", required=True)
    parser.add_argument("--encodings", required=True, type=Path)
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit) is not None, "expected full producer Git commit")
    source, destination = args.source.resolve(), args.destination.resolve()
    require(source != destination and source not in destination.parents, "do not overwrite source delivery")
    prepared = {direction: validate_package(source, direction, args.encodings) for direction in ("ntt", "intt")}
    for direction, package in prepared.items():
        write_package(source, destination, direction, package)
    (destination / "producer_commit.txt").write_text(args.producer_commit + "\n", encoding="ascii")
    shutil.copyfile(args.encodings, destination / "encoder_words.tsv")
    print("transform import: NTT/INTT N4096 Q0, 2 x 16 DMA bindings, physical layout and mathematical golden verified")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, KeyError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"transform import failed: {error}") from error
