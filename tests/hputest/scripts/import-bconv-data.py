#!/usr/bin/env python3
"""接收 producer 的 Q4→P3 FastBConv 程序、真实数据和已解析 DMA，禁止假用精确 CRT。"""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import re
import shutil
import struct

from hpu_opcode_mapping import map_c, map_word
from program_encoding import check_program

N, LINE_BYTES, WINDOW_LINES = 4096, 256, 2048
MODULI = [50061313, 50077697, 50307073, 50552833, 90062849, 90095617, 90218497]
GEOMETRY = {
    "images/input_q.u32.bin": (0, 256),
    "images/constants/qhat_inv_q.u32.bin": (256, 256),
    "images/constants/qhat_mod_p.u32.bin": (512, 768),
    "images/runtime/normalized_q.u32.bin": (1280, 256),
    "images/runtime/output_p.u32.bin": (1536, 192),
    "constants/mod_ctx.u32.bin": (1728, 1),
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def physical_coefficients(logical: list[int]) -> list[int]:
    """独立实现交付 ABI：每个 RNS 分量各自位反转，不能反转跨分量的大数组。"""
    require(len(logical) % N == 0, "incomplete coefficient polynomial")
    bits = N.bit_length() - 1
    positions = [int(f"{index:0{bits}b}"[::-1], 2) for index in range(N)]
    return [logical[base + index] for base in range(0, len(logical), N) for index in positions]


def expected_program() -> list[str]:
    result = ["dload x10, x11, p4, 2, 1"]
    for source in range(4):
        result += [f"pmodld {source}", "dload x10, x11, p0, 1, 0",
                   "dload x10, x11, p1, 1, 0", "pmul p0, p0, p1", "pfree p1",
                   "dstore x10, x11, p0, 1"]
    for target in range(3):
        result.append(f"pmodld {4 + target}")
        for source in range(4):
            result += ["dload x10, x11, p0, 1, 0", "dload x10, x11, p1, 1, 0",
                       ("pmul" if source == 0 else "pmac") + " p2, p0, p1",
                       "pfree p0", "pfree p1"]
        result.append("dstore x10, x11, p2, 1")
    return result + ["pfree p4", "psync"]


def validate(source: Path, encodings: Path) -> dict[str, object]:
    package = source / "bconv"
    data = package / "test_data"
    hardware = data / "hardware"
    params = json.loads((data / "params.json").read_text(encoding="utf-8"))
    abi = json.loads((hardware / "abi.json").read_text(encoding="utf-8"))
    require(params.get("operation") == "bconv" and params.get("N") == N and
            params.get("moduli") == MODULI and params.get("input_domain") == "coefficient/Q" and
            params.get("output_domain") == "coefficient/P", "expected producer Q4 to P3 N4096 package")
    require("coefficient domain bit-reversed" in params.get("hardware_layout", ""),
            "expected bit-reversed coefficient-domain hardware layout")
    require(abi.get("line_bytes") == LINE_BYTES and abi.get("coefficient_bits") == 32 and
            abi.get("byte_order") == "little-endian", "unsupported coefficient ABI")
    line_map = rows(hardware / "line_map.csv")
    require(len(line_map) == len(GEOMETRY), "BConv line map changed; explicit binding review required")
    producer_image = (hardware / "hpu_mem_image.u32.bin").read_bytes()
    require(len(producer_image) == 1729 * LINE_BYTES, "unexpected producer BConv window length")
    images = {}
    for path, (line, count) in GEOMETRY.items():
        selected = [row for row in line_map if row["path"] == path]
        require(len(selected) == 1, f"missing/duplicate line map path {path}")
        row = selected[0]
        raw = (hardware / path).read_bytes()
        require(int(row["line_offset"]) == line and int(row["line_count"]) == count and
                len(raw) == count * LINE_BYTES and int(row["padded_bytes"]) == len(raw),
                f"{path}: geometry mismatch")
        require(producer_image[line * LINE_BYTES:(line + count) * LINE_BYTES] == raw,
                f"{path}: mismatch against unified producer image")
        images[path] = list(struct.unpack(f"<{len(raw) // 4}I", raw))

    inputs = images["images/input_q.u32.bin"]
    math_inputs = (data / "input_q.bin").read_bytes()
    require(len(math_inputs) == 4 * N * 8, "input_q must contain 4x4096 uint64 canonical values")
    logical_inputs = list(struct.unpack(f"<{4 * N}Q", math_inputs))
    require(physical_coefficients(logical_inputs) == inputs,
            "hardware Q input differs from per-basis bit-reversed mathematical input")
    inverses = images["images/constants/qhat_inv_q.u32.bin"]
    target_constants = images["images/constants/qhat_mod_p.u32.bin"]
    normalized = []
    hats = []
    for source_index, modulus in enumerate(MODULI[:4]):
        values = logical_inputs[source_index * N:(source_index + 1) * N]
        require(all(value < modulus for value in values), "noncanonical Q source limb")
        hat = math.prod(q for index, q in enumerate(MODULI[:4]) if index != source_index)
        hats.append(hat)
        inverse = pow(hat % modulus, -1, modulus)
        require(inverses[source_index * N:(source_index + 1) * N] == [inverse] * N,
                f"Q{source_index}: source-hat inverse mismatch")
        normalized.extend(value * inverse % modulus for value in values)
    for target, modulus in enumerate(MODULI[4:]):
        for source_index, hat in enumerate(hats):
            offset = (target * 4 + source_index) * N
            require(target_constants[offset:offset + N] == [hat % modulus] * N,
                    f"P{target}/Q{source_index}: target-hat constant mismatch")
    expected_blob = (data / "expected_p.bin").read_bytes()
    require(len(expected_blob) == 3 * N * 8, "expected_p must contain 3x4096 uint64 canonical values")
    golden = list(struct.unpack(f"<{3 * N}Q", expected_blob))
    for target, modulus in enumerate(MODULI[4:]):
        for index in range(N):
            # FastBConv 的逐模累加，不先恢复CRT大整数；与上游数学契约一致。
            expected = sum(normalized[j * N + index] * (hats[j] % modulus) for j in range(4)) % modulus
            require(golden[target * N + index] == expected,
                    f"FastBConv producer golden mismatch: target={target} coefficient={index}")
    mod_context = images["constants/mod_ctx.u32.bin"]
    expected_mod = []
    for modulus in MODULI:
        mu = (1 << 64) // modulus
        expected_mod += [modulus, mu & 0xffffffff, mu >> 32, 0]
    require(mod_context[:28] == expected_mod and not any(mod_context[28:]), "invalid modulus records")
    require(not any(images["images/runtime/normalized_q.u32.bin"]) and
            not any(images["images/runtime/output_p.u32.bin"]), "producer scratch must begin as zero")

    program = expected_program()
    c_source = (package / "bconv.c").read_text(encoding="utf-8")
    require(re.findall(r"/\* (\d+): (.*?) \*/", c_source) ==
            [(str(index), asm) for index, asm in enumerate(program)],
            "producer BConv sequence changed; cannot reuse fixed DMA contract")
    words = [int(bits, 2) for bits in (package / "bconv.inst32").read_text().split()]
    require(words == [int(word, 16) for word in re.findall(r"\.word 0x([0-9A-Fa-f]{8})", c_source)] and
            len(words) == len(program), "C/inst32 mismatch")
    check_program(program, words, encodings)
    commands = [int(bits, 2) for bits in (package / "bconv.cmd26").read_text().split()]
    require(commands == [(word >> 7) | ((1 << 25) if word & 0x7f == 0x2b else 0) for word in words],
            "cmd26 mismatch")
    plan = rows(data / "dma_plan.csv")
    relocation = rows(package / "dma_relocation_manifest.csv")
    dma_instructions = [index for index, asm in enumerate(program) if asm.startswith(("dload", "dstore"))]
    require(len(plan) == len(relocation) == len(dma_instructions) == 40, "expected 40 DMA rows")
    require(c_source.count('__asm__("x10")') == 40 and c_source.count('__asm__("x11")') == 40,
            "missing DMA x10/x11 bindings")
    expected_spans = [("constants/mod_ctx.u32.bin", 1728, 1)]
    for source_index in range(4):
        expected_spans += [("images/input_q.u32.bin", 64 * source_index, 64),
                           ("images/constants/qhat_inv_q.u32.bin", 256 + 64 * source_index, 64),
                           ("images/runtime/normalized_q.u32.bin", 1280 + 64 * source_index, 64)]
    for target in range(3):
        for source_index in range(4):
            expected_spans += [("images/runtime/normalized_q.u32.bin", 1280 + 64 * source_index, 64),
                               ("images/constants/qhat_mod_p.u32.bin", 512 + 64 * (target * 4 + source_index), 64)]
        expected_spans.append(("images/runtime/output_p.u32.bin", 1536 + 64 * target, 64))
    for index, (span, encoded, instruction) in enumerate(zip(plan, relocation, dma_instructions)):
        require(span["status"] == "RESOLVED" and int(span["dma_index"]) == int(encoded["dma_index"]) == index and
                int(span["instruction_index"]) == int(encoded["instruction_index"]) == instruction and
                span["direction"] == encoded["direction"] and span["object_slot"] == encoded["obj_id"] and
                encoded["normalized_asm"] == program[instruction] and int(encoded["word_hex"], 0) == words[instruction],
                f"DMA {index}: encoded/resolved plan mismatch")
        require(encoded["rs1"] == "x10" and encoded["rs2"] == "x11", "wrong source registers")
        path = span["artifact"]
        require(path in GEOMETRY, f"DMA {index}: unexpected artifact")
        base, available = GEOMETRY[path]
        line, count = int(span["line_offset"]), int(span["line_count"])
        require((path, line, count) == expected_spans[index],
                f"DMA {index}: logical source/target basis binding mismatch")
        require(count == (1 if path == "constants/mod_ctx.u32.bin" else 64) and
                base <= line and line + count <= base + available, f"DMA {index}: outside artifact")
        require(span["direction"] != "dstore" or path in
                ("images/runtime/normalized_q.u32.bin", "images/runtime/output_p.u32.bin"),
                f"DMA {index}: unexpected write destination")
    # 接收端添加 poison 和尾部guard，明确区别于未改动保存的producer原镜像。
    window = list(struct.unpack(f"<{len(producer_image) // 4}I", producer_image))
    window.extend(((0xA5830000 ^ (i * 0x45D9F3B)) & 0xffffffff)
                  for i in range(len(window), WINDOW_LINES * 64))
    window[1280 * 64:1728 * 64] = [0xDEADBEEF] * ((1728 - 1280) * 64)
    # 数学 golden 始终在自然序验证。只有写入 AM 比对表时才转换成与 DSTORE 相同的物理序。
    return dict(window=struct.pack(f"<{len(window)}I", *window),
                golden=struct.pack(f"<{len(golden)}I", *physical_coefficients(golden)),
                normalized=struct.pack(f"<{len(normalized)}I", *physical_coefficients(normalized)),
                plan=plan, c_source=map_c(c_source), words=[map_word(word) for word in words])


def publish(source: Path, destination: Path, prepared: dict[str, object], commit: str) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for field, name in (("window", "window.u32.bin"), ("golden", "golden_p.u32.bin"),
                        ("normalized", "normalized_cpu.u32.bin")):
        (destination / name).write_bytes(prepared[field])
    (destination / "bconv.c").write_text(prepared["c_source"], encoding="utf-8")
    shutil.copyfile(source / "bconv" / "bconv.h", destination / "bconv.h")
    (destination / "bconv.inst32").write_text("".join(f"{word:032b}\n" for word in prepared["words"]), encoding="ascii")
    header = ['#ifndef HPU_BCONV_DELIVERY_H', '#define HPU_BCONV_DELIVERY_H',
              '#include "bconv.h"', '#include <hpu/bconv_case.h>',
              'static const uint32_t bconv_moduli[7] = {' + ', '.join(f'{q}U' for q in MODULI) + '};',
              'static const hpu_dma_span_t bconv_spans[40] = {']
    for row in prepared["plan"]:
        header.append(f'    {{{row["line_offset"]}U, {row["line_count"]}U}}, /* {row["logical_object"]} */')
    header += ['};', 'static const char *const bconv_dma_objects[40] = {']
    header += [f'    "{row["direction"]} p{row["object_slot"]} {row["logical_object"]}",' for row in prepared["plan"]]
    header += ['};', '#endif', '']
    (destination / "bconv_delivery.h").write_text("\n".join(header), encoding="utf-8")
    with (destination / "resolved_dma.tsv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(prepared["plan"][0]), delimiter="\t")
        writer.writeheader()
        writer.writerows(prepared["plan"])
    (destination / "producer_commit.txt").write_text(commit + "\n", encoding="ascii")
    provenance = destination / "upstream"
    names = [f"bconv.{extension}" for extension in ("c", "h", "asm", "inst32", "cmd26")]
    names += ["dma_relocation_manifest.csv", "test_data/params.json", "test_data/dma_plan.csv",
              "test_data/expected_p.bin", "test_data/input_q.bin", "test_data/hardware/abi.json",
              "test_data/hardware/line_map.csv"]
    names += [f"test_data/hardware/{path}" for path in GEOMETRY]
    for name in names:
        copied = provenance / name
        copied.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / "bconv" / name, copied)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--producer-commit", required=True)
    parser.add_argument("--encodings", required=True, type=Path)
    args = parser.parse_args()
    source, destination = args.source.resolve(), args.destination.resolve()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit) is not None, "expected full producer Git commit")
    require(source != destination and source not in destination.parents, "do not overwrite producer data")
    prepared = validate(source, args.encodings)
    publish(source, destination, prepared, args.producer_commit)
    shutil.copyfile(args.encodings, destination / "encoder_words.tsv")
    print("BConv Q4->P3 N4096: bit-reversed coefficient layout, 40 resolved DMA, "
          "source constants and natural-order FastBConv golden verified")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, KeyError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"BConv import failed: {error}") from error
