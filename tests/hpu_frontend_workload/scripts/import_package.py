"""新前端包接收器：保持native指令和布局，支持scratch allocation内的DMA子区间。"""
import csv
import json
from pathlib import Path
import re
import shutil
import struct

GUARD_LINES = 64
NTT_ABI = {
    "version": 1, "coefficient_order": "natural", "pre_twist_order": "natural",
    "post_factor_order": "natural", "stage_twiddle_order": "reference_loader_batch_lane",
    "canonical_ntt_layout": "reference_forward_layout",
    "normalization": "explicit_post_pmul", "twist_fused": False,
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def read_rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def safe_file(root, relative):
    name = Path(relative)
    require(not name.is_absolute() and ".." not in name.parts, "unsafe package path")
    path = (root / name).resolve()
    require(path.is_relative_to(root.resolve()) and path.is_file(), f"missing file: {name}")
    return path


def validate_dma(words, dma, allocations, operations, used):
    """允许arena子区间，但禁止越界、写只读区和丢失指令/应用图来源。"""
    by_id = {row["allocation_id"]: row for row in allocations}
    require(len(by_id) == len(allocations), "duplicate allocation")
    require([int(row["instruction_index"]) for row in dma] ==
            [i for i, word in enumerate(words) if word & 0x7f == 0x2b], "DMA order mismatch")
    writable, first_access = set(), {}
    for index, row in enumerate(dma):
        allocation = by_id.get(row["allocation_id"])
        require(allocation is not None and int(row["dma_index"]) == index, "DMA allocation/index")
        first, count = int(row["line_offset"]), int(row["line_count"])
        base, size = int(allocation["line_offset"]), int(allocation["line_count"])
        require(count > 0 and base <= first and first + count <= base + size <= used,
                "DMA subspan outside allocation/window")
        inst = words[int(row["instruction_index"])]
        require(inst == int(row["word_hex"], 16) and (inst >> 15) & 31 == 10 and
                (inst >> 20) & 31 == 11, "DMA word/register ABI mismatch")
        require(row["direction"] in ("dload", "dstore"), "invalid DMA direction")
        for line in range(first, first + count):
            first_access.setdefault(line, row["direction"])
        if row["direction"] == "dstore":
            require(allocation["read_only"] == "false", "DSTORE into readonly allocation")
            writable.update(range(first, first + count))
        op_index = row["operation_index"]
        require((not op_index and row["operation_id"] == "$application") or
                (op_index and 0 <= int(op_index) < len(operations) and
                 operations[int(op_index)]["id"] == row["operation_id"]), "DMA graph mismatch")
    return writable, first_access


def validate_host(report):
    require(report.get("overall_status") == "pass" and all(report.get(k) is True for k in
            ("oracle_verified", "golden_matches_oracle", "model_verified", "raw_physical_words_equal",
             "host_instruction_model_verified", "optimization_equivalence_verified")),
            "frontend oracle/model/equivalence not verified")
    model = report.get("host_instruction_model", {})
    require(model.get("status") == "pass" and model.get("required") is True and
            model.get("verified") is True and model.get("ntt_contract") == "natural_coefficient_boundaries",
            "strict natural-boundary instruction verification required")
    require(model.get("hardware_contract_verified") is False and all(report.get(k) is False for k in
            ("instruction_execution_verified", "rtl_verified", "hardware_verified")),
            "host package must not claim hardware execution")


def import_package(source, destination, spec, commit):
    package = read_json(source / "package.json")
    scheme, degree = spec["scheme"], int(spec["degree"])
    stem = f"{scheme}_evaluator_application" + (f"_n{degree}" if degree != 128 else "")
    require(package.get("schema") == "hpu-application-package" and package.get("schema_version") == 1 and
            package.get("scheme") == scheme and package.get("case_name") == stem and
            [package.get(k) for k in ("word_bits", "line_words", "line_bytes", "byte_order")] ==
            [32, 64, 256, "little-endian"], "frontend package ABI/case mismatch")
    keys = ("parameters", "operation_graph", "program_source", "program_header", "program_inst32",
            "dma_relocation_manifest", "image", "line_map", "memory_manifest", "memory_config",
            "golden_manifest", "oracle_report")
    files = {key: safe_file(source, package[key]) for key in keys}
    provenance = read_json(source / "provenance/build.json")
    require(provenance.get("commit") == commit and
            provenance.get("worktree_state") == "clean-at-configure", "producer revision/cleanliness mismatch")
    parameters, report = read_json(files["parameters"]), read_json(files["oracle_report"])
    require(parameters.get("scheme") == scheme and parameters.get("poly_modulus_degree") == degree and
            parameters.get("ntt_table_abi") == NTT_ABI, "new NTT table ABI required")
    validate_host(report)
    config = read_json(files["memory_config"])
    used = config["used_lines"]
    require(type(used) is int and 0 < used == config["capacity_lines"] < 1 << 32,
            "image must be trimmed to actual lines")
    # ELF镜像从0x80000000加载，DDR从0x87000000起；构建后另做实际ELF段重叠检查。
    require((used + GUARD_LINES) * 256 < 0x07000000, "image exceeds workload memory budget")
    image = bytearray(files["image"].read_bytes())
    require(len(image) == used * 256, "image length mismatch")
    operations = read_json(files["operation_graph"])["operations"]
    allocations = read_rows(files["line_map"])
    fields = ("allocation_id", "kind", "read_only", "line_offset", "line_count",
              "payload_words", "padded_words", "initialization")
    require([{key: row[key] for key in fields} for row in allocations] ==
            [{key: row[key] for key in fields} for row in read_rows(files["memory_manifest"])],
            "memory manifests differ")
    end = 0
    for allocation in sorted(allocations, key=lambda row: int(row["line_offset"])):
        first, count = int(allocation["line_offset"]), int(allocation["line_count"])
        require(end <= first and count > 0 and first + count <= used and
                int(allocation["padded_words"]) == count * 64, "overlapping/malformed allocation")
        end = first + count
    words = [int(word, 2) for word in files["program_inst32"].read_text().split()]
    code = files["program_source"].read_text()
    require([int(word, 16) for word in re.findall(r'\.word (0x[0-9A-Fa-f]+)', code)] == words,
            "producer C/INST32 mismatch")
    require(words and words[-1] == 0x7000005b and words.count(0x7000005b) == 1 and
            all(word & 0x7f in (0x2b, 0x5b) for word in words), "opcode/terminal PSYNC mismatch")
    dma = read_rows(files["dma_relocation_manifest"])
    spans = [(int(a), int(b)) for a, b in re.findall(
        r'\{ UINT32_C\((\d+)\), UINT32_C\((\d+)\) \}', code)]
    require(spans == [(int(row["line_offset"]), int(row["line_count"])) for row in dma],
            "C resolved spans/DMA manifest mismatch")
    writable, first_access = validate_dma(words, dma, allocations, operations, used)
    levels = {tuple(level["parms_id"]): level["moduli"] for level in parameters["levels"]}
    by_span = {(int(row["line_offset"]), int(row["line_count"])): row for row in allocations}
    golden, outputs, order = bytearray(), [], []
    for row in read_rows(files["golden_manifest"]):
        step, component, mod_id = int(row["object_id"].removeprefix("step_")), int(row["component"]), int(row["modulus_id"])
        require(0 <= step < len(operations), "invalid golden step")
        moduli = levels[tuple(operations[step]["parms_id"])]
        require(0 <= mod_id < len(moduli) and 0 <= component < operations[step]["component_count"],
                "invalid golden component/modulus")
        first, count, q = int(row["line_offset"]), int(row["line_count"]), moduli[mod_id]
        allocation = by_span.get((first, count))
        require(allocation is not None and allocation["kind"] == "output" and
                all(first_access.get(line) == "dstore" for line in range(first, first + count)),
                "golden must be a write-first pinned output")
        require(int(row["modulus"]) == q and 0 < q < 1 << 32 and
                int(row["payload_words"]) == int(row["padded_words"]) == count * 64 == degree and
                row["domain"] == ("coefficient" if scheme == "bfv" else "canonical_ntt_physical"),
                "golden shape/domain/modulus mismatch")
        data = safe_file(source, row["path"]).read_bytes()
        require(len(data) == degree * 4 and all(value < q for value, in struct.iter_unpack("<I", data)),
                "noncanonical/truncated golden")
        outputs.append((first, degree, len(golden) // 4, q, step, component, mod_id))
        order.append((step, component, mod_id))
        golden += data
        image[first * 256:(first + count) * 256] = b"\xff" * (count * 256)
    expected_order = [(step, component, mod_id) for step, op in enumerate(operations)
                      for component in range(op["component_count"])
                      for mod_id in range(len(levels[tuple(op["parms_id"])]))]
    require(order == expected_order and len(order) == report["verified_limb_count"], "incomplete golden coverage")
    # 只毒化写先输出；scratch、输入、twiddle和密钥保持原样，无旧BFV/CKKS重排补丁。
    image += struct.pack("<I", 0xa5a55a5a) * (64 * GUARD_LINES)
    require(not destination.exists(), "refusing to overwrite imported delivery")
    destination.mkdir(parents=True)
    (destination / "image.u32.bin").write_bytes(image)
    (destination / "golden.u32.bin").write_bytes(golden)
    (destination / "writable.u8.bin").write_bytes(bytes(int(line in writable) for line in range(used + GUARD_LINES)))
    for key, suffix in (("program_source", "c"), ("program_header", "h")):
        shutil.copy2(files[key], destination / f"{stem}.{suffix}")
    (destination / "program.h").write_text(f'#include "{stem}.h"\n')
    constants = {"DEGREE": degree, "WINDOW_LINES": used, "IMAGE_LINES": used + GUARD_LINES,
                 "GOLDEN_WORDS": len(golden) // 4, "OUTPUT_COUNT": len(outputs),
                 "INSTRUCTION_COUNT": len(words), "DMA_COUNT": len(dma)}
    header = '#ifndef FRONTEND_LAYOUT_H\n#define FRONTEND_LAYOUT_H\n#include <stdint.h>\n'
    header += f'#define CASE_ID "{spec["case_id"]}"\n'
    header += "\n".join(f"#define {name} {value}U" for name, value in constants.items())
    header += '\nstruct output_span { unsigned line, words, golden_word, modulus, step, component, modulus_id; };\n'
    header += 'extern const struct output_span outputs[OUTPUT_COUNT];\n#endif\n'
    (destination / "layout.h").write_text(header)
    table = '#include "layout.h"\nconst struct output_span outputs[OUTPUT_COUNT] = {\n'
    table += "\n".join("    { " + ", ".join(f"{value}U" for value in row) + " }," for row in outputs)
    (destination / "layout.c").write_text(table + "\n};\n")
    metadata = {**spec, **constants, "program_stem": stem, "producer_commit": commit,
                "ntt_table_abi": NTT_ABI, "hardware_verified": False,
                "status": "BUILD_READY_NOT_IT_PASS", "layout_adaptation": "none",
                "compiler_resources": {key: value for key, value in report["compiler_resources"].items()
                                       if key != "logical_buffers"}}
    (destination / "delivery.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata
