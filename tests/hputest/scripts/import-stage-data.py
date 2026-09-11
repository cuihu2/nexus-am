#!/usr/bin/env python3
"""导入并逐项校验真实 producer 的六张单 stage twiddle 表，不生成替代数据。"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import struct

from hpu_ntt_layout import CONVENTION, HARDWARE_LAYOUT, bit_reverse, expected_twiddles

N = 4096
Q = 50061313
LINE_BYTES = 256
STAGES = (0, 1, 11)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def unique_row(items: list[dict[str, str]], **fields: str) -> dict[str, str]:
    selected = [row for row in items if all(row.get(k) == v for k, v in fields.items())]
    require(len(selected) == 1, f"expected exactly one manifest row: {fields}")
    return selected[0]


def validate(source: Path) -> tuple[list[dict[str, object]], list[tuple[str, bytes]]]:
    nt = source / "ntt" / "test_data"
    hw = nt / "hardware"
    params = json.loads((nt / "params.json").read_text(encoding="utf-8"))
    mm = json.loads((source / "mm" / "test_data" / "params.json").read_text(encoding="utf-8"))
    abi = json.loads((hw / "abi.json").read_text(encoding="utf-8"))
    for name, metadata in (("ntt", params), ("mm", mm)):
        require(metadata.get("N") == N and metadata.get("moduli") == [Q],
                f"{name}: expected N={N}, single q={Q}")
        require(HARDWARE_LAYOUT in metadata.get("hardware_layout", ""),
                f"{name}: obsolete/unknown memory layout")
    require(params.get("operation") == "ntt" and mm.get("operation") == "mm",
            "producer operation metadata mismatch")
    require(abi.get("N") == N and abi.get("coefficient_bits") == 32 and
            abi.get("byte_order") == "little-endian" and abi.get("line_bytes") == LINE_BYTES,
            "unsupported hardware ABI geometry")
    require(abi.get("twiddle", {}).get("convention") == CONVENTION,
            "unsupported stage twiddle convention")
    table = rows(hw / "twiddle_map.csv")
    line_map = rows(hw / "line_map.csv")
    pre = unique_row(table, direction="ntt", basis_index="0", phase="pre_twist", stage="-1")
    require(pre["path"] == "constants/twiddle/ntt/basis_00/pre_twist.u32.bin" and
            int(pre["value_count"]) == N and int(pre["line_count"]) == 64 and
            pre["loader_mode"] == "pointwise", "pre_twist manifest geometry mismatch")
    pre_raw = (hw / pre["path"]).read_bytes()
    require(len(pre_raw) == N * 4, "pre_twist must contain N words")
    pre_words = struct.unpack(f"<{N}I", pre_raw)
    # 新清单不再使用错误的等比数列字段；physical[N/2] 对应 logical[1]。
    psi = pre_words[N // 2]
    require(int(pre["modulus"]) == Q and pow(psi, N, Q) == Q - 1 and
            pow(psi, 2 * N, Q) == 1, "invalid primitive 2N-th root in pre_twist metadata")
    require(list(pre_words) == [pow(psi, bit_reverse(i, N), Q) for i in range(N)],
            "pre_twist does not use bit-reversed coefficient layout")
    expected_tables = expected_twiddles(N, Q, psi)
    full_image = (hw / "hpu_mem_image.u32.bin").read_bytes()
    pre_offset = int(pre["line_offset"]) * LINE_BYTES
    require(full_image[pre_offset:pre_offset + len(pre_raw)] == pre_raw,
            "pre_twist differs from unified producer image")
    selection: list[dict[str, object]] = []
    files: list[tuple[str, bytes]] = []
    for direction in ("ntt", "intt"):
        for stage in STAGES:
            entry = unique_row(table, direction=direction, basis_index="0",
                               phase="butterfly", stage=str(stage))
            path = f"constants/twiddle/{direction}/basis_00/stage_{stage:02d}.u32.bin"
            require(entry["path"] == path, f"unexpected source path: {entry['path']}")
            forward_stage = stage if direction == "ntt" else 11 - stage
            expected_fields = {"modulus": Q, "value_count": N // 2,
                               "forward_stage": forward_stage, "batch_count": N // 128,
                               "lanes_per_batch": 64, "line_count": 32}
            for key, value in expected_fields.items():
                require(int(entry[key], 0) == value,
                        f"{direction}/stage{stage}: wrong {key}, expected {value}")
            require(entry["loader_mode"] == ("sequential_128" if forward_stage < 7 else "interleaved_64x2"),
                    f"{direction}/stage{stage}: wrong loader mode")
            geometry = unique_row(line_map, path=path)
            for key, value in (("line_count", 32), ("payload_words", N // 2),
                               ("payload_bytes", 8192), ("padded_words", N // 2),
                               ("padded_bytes", 8192), ("line_offset", int(entry["line_offset"]))):
                require(int(geometry[key], 0) == value, f"{path}: wrong line_map {key}")
            raw = (hw / path).read_bytes()
            require(len(raw) == 8192, f"{path}: expected 2048 uint32 words")
            offset = int(entry["line_offset"]) * LINE_BYTES
            require(full_image[offset:offset + len(raw)] == raw,
                    f"{path}: table differs from unified producer image")
            actual = struct.unpack("<2048I", raw)
            for index, value in enumerate(expected_tables[direction][stage]):
                require(actual[index] == value,
                        f"{direction}/stage{stage}: word={index} actual={actual[index]} expected={value}")
            destination = f"{direction}_stage_{stage:02d}.u32.bin"
            files.append((destination, raw))
            selection.append(dict(direction=direction, stage=stage, modulus=Q,
                                  words=N // 2, lines=32, forward_stage=forward_stage,
                                  loader_mode=entry["loader_mode"],
                                  source=f"ntt/test_data/hardware/{path}", destination=destination))
    return selection, files


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="同批 producer outputs 根目录")
    parser.add_argument("--destination", required=True, type=Path, help="生成目录 instruction-data")
    parser.add_argument("--producer-commit", required=True, help="同批 producer 的固定提交号")
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit) is not None,
            "producer commit must be a full 40-character Git commit")
    source = args.source.resolve()
    destination = args.destination.resolve()
    require(source != destination and source not in destination.parents,
            "destination must not overwrite producer data")
    selection, files = validate(source)
    # 全部验证通过后再写接收目录；只写本脚本拥有的六张表和来源清单。
    destination.mkdir(parents=True, exist_ok=True)
    for filename, raw in files:
        (destination / filename).write_bytes(raw)
    provenance = destination / "provenance"
    provenance.mkdir(exist_ok=True)
    for filename in ("params.json", "hardware/abi.json", "hardware/twiddle_map.csv", "hardware/line_map.csv"):
        shutil.copyfile(source / "ntt" / "test_data" / filename,
                        provenance / Path(filename).name)
    shutil.copyfile(source / "mm" / "test_data" / "params.json", provenance / "mm_params.json")
    # 新清单不单列 psi，保留用于独立求根/校验的原始 pre-twist 表。
    shutil.copyfile(source / "ntt/test_data/hardware/constants/twiddle/ntt/basis_00/pre_twist.u32.bin",
                    provenance / "pre_twist.u32.bin")
    (provenance / "producer_commit.txt").write_text(args.producer_commit + "\n", encoding="utf-8")
    with (destination / "selection.tsv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(selection[0]), delimiter="\t")
        writer.writeheader()
        writer.writerows(selection)
    print(f"stage data: validated and imported {len(files)} producer tables, N={N}, q={Q}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"stage data import failed: {error}") from error
