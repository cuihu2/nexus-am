#!/usr/bin/env python3
"""接收 main 的两个 CKKS 完整应用；保留 producer C/编码/布局，不合并成多轮测试。"""
import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess


PROFILES = {
    "polynomial": ("ckks_polynomial_x2_plus_one", 65536, 3,
                   "output/x_squared_plus_one/q3/", 4388, 1652),
    "composed": ("ckks_composed_application", 128, 2,
                  "output/y/next/", 4411, 1774),
    "reline": ("ckks_reline", 4096, 4,
                "output/relinearized/", 2753, 1055),
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def rows(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def safe_file(root, relative):
    path = (root / relative).resolve()
    require(path.is_relative_to(root.resolve()) and path.is_file(),
            f"invalid artifact path: {relative}")
    return path


def validate(source, profile, encoder, producer_commit=None):
    stem, n, q_count, prefix, instructions, dma_count = PROFILES[profile]
    hardware = source / "test_data/hardware"
    config = json.loads((hardware / "hpu_mem_config.json").read_text())
    for key, value in {"format_version": 1, "scheme": "CKKS", "program_stem": stem,
                       "entry_point": "hpu_run_" + stem, "word_bytes": 4,
                       "line_words": 64, "line_bytes": 256, "byte_order": "little-endian",
                       "instruction_count": instructions, "dma_count": dma_count,
                       "dma_offset_register": "x10", "dma_length_register": "x11",
                       "dma_address_unit": "256-byte-line"}.items():
        require(config.get(key) == value, f"unexpected CKKS {key}")
    for entry in rows(source / "artifact_manifest.csv"):
        require(safe_file(source, entry["path"]).stat().st_size == int(entry["byte_count"]),
                f"artifact size mismatch: {entry['path']}")
    subprocess.run([str(encoder), str(source / (stem + ".asm")),
                    str(source / (stem + ".inst32")), str(source / (stem + ".cmd26"))],
                   check=True)
    words = [int(w, 2) for w in (source / (stem + ".inst32")).read_text().split()]
    code = (source / (stem + ".c")).read_text()
    c_words = [int(w, 16) for w in re.findall(r'\.word (0x[0-9A-Fa-f]+)', code)]
    require(c_words == words and len(words) == instructions, "C instruction mismatch")
    require(words[-1] == 0x7000005b and words.count(0x7000005b) == 1,
            "one terminal PSYNC required")
    require(all(w & 0x7f in (0x2b, 0x5b) for w in words), "obsolete/unknown opcode")
    image = bytearray((hardware / "hpu_mem_image.u32.bin").read_bytes())
    used = config["image_used_lines"]
    require(len(image) == used * 256 and used + 64 <= config["hpu_mem_capacity_lines"],
            "image/window bounds mismatch")
    allocations = rows(hardware / "line_map.csv")
    by_id = {r["allocation_id"]: r for r in allocations}
    require(len(by_id) == len(allocations), "duplicate allocations")
    previous = 0
    for entry in sorted(allocations, key=lambda r: int(r["line_offset"])):
        first, count = int(entry["line_offset"]), int(entry["line_count"])
        require(count > 0 and previous <= first and first + count <= used,
                "overlapping/out-of-window allocations")
        require(int(entry["byte_offset"]) == first * 256 and
                int(entry["padded_bytes"]) == count * 256 and
                int(entry["word_count"]) * 4 <= count * 256, "allocation shape")
        previous = first + count
    dma = rows(source / "dma_relocation_manifest.csv")
    require(len(dma) == dma_count, "DMA count mismatch")
    spans = [(int(a), int(b)) for a, b in re.findall(
        r'\{ UINT32_C\((\d+)\), UINT32_C\((\d+)\) \}', code)]
    require(spans == [(int(r["line_offset"]), int(r["line_count"])) for r in dma],
            "C resolved spans differ from DMA manifest")
    require([int(r["instruction_index"]) for r in dma] ==
            [i for i, w in enumerate(words) if w & 0x7f == 0x2b], "DMA order mismatch")
    writable, first_access = set(), {}
    for index, entry in enumerate(dma):
        name = entry["allocation_id"]
        require(name in by_id and int(entry["dma_index"]) == index, "DMA allocation/index")
        allocation = by_id[name]
        first, count = int(entry["line_offset"]), int(entry["line_count"])
        require(first == int(allocation["line_offset"]) and
                count == int(allocation["line_count"]), "partial/unbound DMA")
        require(words[int(entry["instruction_index"])] == int(entry["word_hex"], 16),
                "DMA instruction mismatch")
        require(entry["direction"] in ("dload", "dstore"), "DMA direction")
        first_access.setdefault(name, entry["direction"])
        if entry["direction"] == "dstore":
            require(allocation["read_only"] == "0", "DSTORE into readonly allocation")
            writable.update(range(first, first + count))
    if profile == "reline":
        metadata = json.loads((source / "CMB012_METADATA.json").read_text())
        expected = {
            "format_version": 1,
            "case_id": "HPU_IT_DIR_CMB_012",
            "scheme": "CKKS",
            "api": "hpu::seal_adapter::CkksOperationPlan::append_relinearize",
            "poly_modulus_degree": 4096,
            "q_count": 4,
            "special_modulus_count": 1,
            "input_component_count": 3,
            "output_component_count": 2,
            "domain": "canonical_ntt_physical",
            "instruction_count": instructions,
            "dma_count": dma_count,
            "image_used_lines": used,
            "guard_lines": 64,
        }
        for key, value in expected.items():
            require(metadata.get(key) == value, f"unexpected CMB012 metadata {key}")
        commit = metadata.get("producer_commit", "")
        require(re.fullmatch(r"[0-9a-f]{40}", commit),
                "invalid CMB012 producer commit")
        if producer_commit is not None:
            require(commit == producer_commit, "CMB012 producer commit mismatch")
        require({entry["operation_id"] for entry in dma}
                <= {"$application", "relinearize"},
                "CMB012 delivery contains a non-Reline operation")
        tensor_names = [f"input/tensor/c{c}/mod{q}"
                        for c in range(3) for q in range(q_count)]
        require(all(name in by_id and by_id[name]["read_only"] == "0"
                    and first_access.get(name) == "dload" for name in tensor_names),
                "CMB012 writable tensor input is missing or not read first")
    outputs = {r["allocation_id"]: r for r in rows(hardware / "expected_outputs.csv")}
    final_names = [f"{prefix}c{c}/mod{q}" for c in range(2) for q in range(q_count)]
    require(sorted(k for k in by_id if k.startswith(prefix)) == sorted(final_names),
            "final output component/basis shape changed")
    golden, final_spans = bytearray(), []
    for name in final_names:
        require(name in outputs and first_access.get(name) == "dstore",
                "missing final golden or final output read before write")
        entry, allocation = outputs[name], by_id[name]
        first, count = int(allocation["line_offset"]), int(allocation["line_count"])
        require(int(entry["line_offset"]) == first and int(entry["line_count"]) == count
                and int(entry["word_count"]) == n and int(allocation["word_count"]) == n,
                "final golden dimensions")
        data = safe_file(hardware, entry["path"]).read_bytes()
        require(len(data) == count * 256 == n * 4, "final golden length")
        final_spans.append((first, count, len(golden) // 4))
        golden += data
        # 只毒化已经证明先写后读的最终输出；保留中间累加初值。
        image[first * 256:(first + count) * 256] = b"\xff" * (count * 256)
    image += struct.pack("<I", 0xA5A55A5A) * (64 * 64)
    # 逐 line 权限掩码：所有非 DSTORE 区（包括尾部 guard）必须保持原值。
    mask = bytes(int(line in writable) for line in range(used + 64))
    return config, image, golden, mask, final_spans


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--encoder", type=Path, required=True)
    parser.add_argument("--producer-commit", required=True)
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit), "invalid producer commit")
    config, image, golden, mask, spans = validate(
        args.source, args.profile, args.encoder, args.producer_commit)
    root = args.destination
    require(not root.resolve().is_relative_to(args.source.resolve()) and
            not args.source.resolve().is_relative_to(root.resolve()), "overlapping import")
    root.mkdir(parents=True, exist_ok=True)
    stem, n, q_count, *_ = PROFILES[args.profile]
    for extension in ("c", "h", "asm", "inst32", "cmd26"):
        shutil.copy2(args.source / f"{stem}.{extension}", root)
    # 发布可追溯的布局/重定位；不再复制 61 MiB 原镜像和全部中间 golden。
    for name in ("line_map.csv", "expected_outputs.csv", "hpu_mem_config.json"):
        shutil.copy2(args.source / "test_data/hardware" / name, root)
    shutil.copy2(args.source / "dma_relocation_manifest.csv", root)
    if args.profile == "reline":
        shutil.copy2(args.source / "CMB012_METADATA.json", root)
    (root / "ckks_window.u32.bin").write_bytes(image)
    (root / "ckks_golden.u32.bin").write_bytes(golden)
    (root / "ckks_writable.u8.bin").write_bytes(mask)
    (root / "producer_commit.txt").write_text(args.producer_commit + "\n")
    declarations = "\n".join(f"    {{{first}U, {count}U, {offset}U}}," for first, count, offset in spans)
    (root / "ckks_layout.h").write_text(
        "#ifndef CKKS_LAYOUT_H\n#define CKKS_LAYOUT_H\n"
        f"enum {{ CKKS_LINES = {len(mask)}U, CKKS_N = {n}U, CKKS_Q = {q_count}U,\n"
        f"       CKKS_OUTPUTS = {len(spans)}U, CKKS_GOLDEN_WORDS = {len(golden)//4}U }};\n"
        "struct ckks_output { unsigned line, lines, golden_word; };\n"
        "static const struct ckks_output ckks_outputs[CKKS_OUTPUTS] = {\n" +
        declarations + "\n};\n#endif\n")
    log = args.source / "HOST_ORACLE.log"
    require(log.is_file(), "missing successful upstream host oracle log")
    shutil.copy2(log, root)
    print(f"CKKS {args.profile}: N={n}, lines={len(mask)}, final_words={len(golden)//4}")


if __name__ == "__main__":
    main()
