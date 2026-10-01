#!/usr/bin/env python3
"""Validate and import supported upstream application-package-v1 cases."""

import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


GUARD_LINES = 64
WORDS_PER_LINE = 64
CASE_SPECS = {
    "bfv_multiply_modswitch_application": {
        "label": "APP004", "scheme": "bfv", "model": "BfvSoftwareExecutor",
        "plain_modulus": 130817,
        "moduli": [1043201, 1043969, 1044737],
        "key_moduli": [1043201, 1043969, 1044737, 1047041],
        "operation_ids": ["multiply", "mod_switch", "add_bias"],
        "operation_kinds": ["multiply", "mod_switch", "add_plain"],
        "final_output": "output/y/next", "capacity_lines": 16384,
        "used_lines": 692, "allocation_count": 411,
        "instruction_count": 4434, "dma_count": 1835,
        "golden_domain": "coefficient",
        "golden_moduli": [
            [1043201, 1043969, 1044737],
            [1043201, 1043969],
            [1043201, 1043969],
        ],
    },
    "bfv_rotation_application": {
        "label": "APP005", "scheme": "bfv", "model": "BfvSoftwareExecutor",
        "plain_modulus": 130817,
        "moduli": [1043201, 1043969, 1044737],
        "key_moduli": [1043201, 1043969, 1044737, 1047041],
        "operation_ids": ["rotate_rows_2", "rotate_columns", "add"],
        "operation_kinds": ["rotate_rows", "rotate_columns", "add"],
        "final_output": "output/y", "capacity_lines": 8192, "used_lines": 464,
        "allocation_count": 317, "instruction_count": 2633, "dma_count": 1057,
        "golden_domain": "coefficient",
        "golden_moduli": [[1043201, 1043969, 1044737]] * 3,
    },
    "bgv_plain_chain": {
        "label": "APP006", "scheme": "bgv", "model": "BgvSoftwareExecutor",
        "plain_modulus": 65537,
        "moduli": [2013265921, 1811939329, 469762049],
        "key_moduli": [2013265921, 1811939329, 469762049, 1224736769],
        "operation_ids": ["add_bias", "multiply_polynomial", "subtract_offset"],
        "operation_kinds": ["add_plain", "multiply_plain", "subtract_plain"],
        "final_output": "output", "capacity_lines": 512, "used_lines": 67,
        "allocation_count": 34, "instruction_count": 90, "dma_count": 46,
        "golden_domain": "canonical_ntt_physical",
        "golden_moduli": [[2013265921, 1811939329, 469762049]] * 3,
    },
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read_rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def safe_file(root, relative):
    relative = Path(relative)
    require(not relative.is_absolute() and ".." not in relative.parts,
            f"unsafe package path: {relative}")
    path = (root / relative).resolve()
    require(path.is_relative_to(root.resolve()) and path.is_file(),
            f"missing package artifact: {relative}")
    return path


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def validate(source, validator, producer_commit=None):
    subprocess.run([str(validator), str(source)], check=True)
    package = load_json(source / "package.json")
    case = package.get("case_name")
    require(case in CASE_SPECS, f"unsupported application case: {case}")
    spec = CASE_SPECS[case]
    expected_package = {
        "schema": "hpu-application-package", "schema_version": 1,
        "scheme": spec["scheme"], "case_name": case, "word_bits": 32,
        "line_words": WORDS_PER_LINE, "line_bytes": 256,
        "byte_order": "little-endian",
    }
    for key, value in expected_package.items():
        require(package.get(key) == value, f"unexpected package field: {key}")
    referenced = (
        "parameters", "operation_graph", "program_source", "program_header",
        "program_asm", "program_inst32", "program_cmd26",
        "dma_relocation_manifest", "image", "line_map", "memory_manifest",
        "memory_abi", "memory_config", "golden_manifest", "oracle_report",
        "semantic_report",
    )
    files = {key: safe_file(source, package[key]) for key in referenced}

    build = load_json(safe_file(source, "provenance/build.json"))
    require(build.get("schema") == "hpu-application-package-build" and
            build.get("schema_version") == 1 and
            build.get("repository") == "inline-asm",
            "invalid application build provenance")
    commit = build.get("commit", "")
    require(re.fullmatch(r"[0-9a-f]{40}", commit) is not None,
            "invalid application producer commit")
    if producer_commit is not None:
        require(commit == producer_commit, "application producer commit mismatch")
    require(build.get("worktree_state") == "clean-at-configure",
            "application was not produced from a clean worktree")

    oracle = load_json(files["oracle_report"])
    golden_count = sum(2 * len(moduli) for moduli in spec["golden_moduli"])
    require(oracle.get("overall_status") == "pass" and
            oracle.get("oracle_verified") is True and
            oracle.get("golden_matches_oracle") is True and
            oracle.get("model") == spec["model"] and
            oracle.get("model_verified") is True and
            oracle.get("raw_physical_words_equal") is True and
            oracle.get("verified_limb_count") == golden_count,
            "SEAL/software-model oracle did not pass")
    checks = {entry.get("name"): entry for entry in oracle.get("checks", [])}
    require(checks.get("seal_oracle_to_golden", {}).get("status") == "pass" and
            checks.get("seal_oracle_to_golden", {}).get("required") is True and
            checks.get("host_software_model_to_oracle", {}).get("status") == "pass" and
            checks.get("host_software_model_to_oracle", {}).get("required") is True and
            checks.get("host_software_model_to_oracle", {}).get("model") ==
            spec["model"], "required application oracle checks are missing")
    require(oracle.get("instruction_execution_verified") is False and
            oracle.get("rtl_verified") is False and
            oracle.get("hardware_verified") is False,
            "package must not claim target/RTL execution")

    parameters = load_json(files["parameters"])
    require(parameters.get("scheme") == spec["scheme"] and
            parameters.get("poly_modulus_degree") == 128 and
            parameters.get("plain_modulus") == spec["plain_modulus"] and
            parameters.get("key_moduli") == spec["key_moduli"],
            f"unexpected {spec['scheme'].upper()} parameters")
    levels = parameters.get("levels", [])
    require(len(levels) == 3 and levels[0].get("moduli") == spec["moduli"],
            f"unexpected {spec['scheme'].upper()} modulus chain")
    graph = load_json(files["operation_graph"])
    operations = graph.get("operations", [])
    require(graph.get("final_output") == spec["final_output"] and
            [entry.get("id") for entry in operations] == spec["operation_ids"] and
            [entry.get("kind") for entry in operations] == spec["operation_kinds"] and
            all(entry.get("component_count") == 2 for entry in operations),
            f"unexpected {spec['label']} operation graph")
    semantic = load_json(files["semantic_report"])
    require(len(semantic.get("decoded", [])) == 128 and
            all(isinstance(value, int) and 0 <= value < spec["plain_modulus"]
                for value in semantic["decoded"]), "invalid decoded semantic oracle")

    config = load_json(files["memory_config"])
    require(config == {"capacity_lines": spec["capacity_lines"],
                       "used_lines": spec["used_lines"]},
            f"unexpected {spec['label']} memory configuration")
    abi = load_json(files["memory_abi"])
    for key, value in {"schema": "hpu-dma-abi", "schema_version": 1,
                       "rs1": "x10", "rs2": "x11", "word_bits": 32,
                       "line_words": 64, "line_bytes": 256,
                       "byte_order": "little-endian"}.items():
        require(abi.get(key) == value, f"unexpected memory ABI field: {key}")

    words = [int(word, 2) for word in files["program_inst32"].read_text().split()]
    code = files["program_source"].read_text(encoding="utf-8")
    c_words = [int(word, 16) for word in re.findall(
        r'\.word (0x[0-9A-Fa-f]+)', code)]
    require(len(words) == spec["instruction_count"] and c_words == words,
            "generated C instruction mismatch")
    require(words[-1] == 0x7000005B and words.count(0x7000005B) == 1,
            "one terminal PSYNC required")
    require(all(word & 0x7F in (0x2B, 0x5B) for word in words),
            "obsolete or unknown instruction opcode")

    allocations = read_rows(files["line_map"])
    manifest = read_rows(files["memory_manifest"])
    require(len(allocations) == spec["allocation_count"] and
            len(manifest) == len(allocations),
            "unexpected allocation count")
    manifest_fields = ("allocation_id", "kind", "read_only", "line_offset",
                       "line_count", "payload_words", "padded_words", "initialization")
    require([{key: row[key] for key in manifest_fields} for row in allocations] ==
            [{key: row[key] for key in manifest_fields} for row in manifest],
            "line map differs from memory manifest")
    by_id = {row["allocation_id"]: row for row in allocations}
    require(len(by_id) == len(allocations), "duplicate allocation ID")
    previous = 0
    for row in sorted(allocations, key=lambda entry: int(entry["line_offset"])):
        first, count = int(row["line_offset"]), int(row["line_count"])
        require(count > 0 and first >= previous and first + count <= config["used_lines"],
                "overlapping or out-of-range allocation")
        require(int(row["padded_words"]) == count * WORDS_PER_LINE and
                int(row["payload_words"]) <= int(row["padded_words"]),
                "invalid allocation dimensions")
        previous = first + count

    dma = read_rows(files["dma_relocation_manifest"])
    require(len(dma) == spec["dma_count"], f"unexpected {spec['label']} DMA count")
    spans = [(int(a), int(b)) for a, b in re.findall(
        r'\{ UINT32_C\((\d+)\), UINT32_C\((\d+)\) \}', code)]
    expected_spans = [(int(row["line_offset"]), int(row["line_count"])) for row in dma]
    require(spans == expected_spans, "C resolved spans differ from DMA manifest")
    require([int(row["instruction_index"]) for row in dma] ==
            [index for index, word in enumerate(words) if word & 0x7F == 0x2B],
            "DMA instruction order mismatch")
    writable, first_access = set(), {}
    for index, row in enumerate(dma):
        allocation = by_id.get(row["allocation_id"])
        require(allocation is not None and int(row["dma_index"]) == index,
                "invalid DMA allocation/index")
        first, count = int(row["line_offset"]), int(row["line_count"])
        require((first, count) == (int(allocation["line_offset"]),
                                   int(allocation["line_count"])),
                "partial or unbound DMA allocation")
        require(words[int(row["instruction_index"])] == int(row["word_hex"], 16),
                "DMA instruction word mismatch")
        direction = row["direction"]
        require(direction in ("dload", "dstore"), "invalid DMA direction")
        first_access.setdefault(row["allocation_id"], direction)
        if direction == "dstore":
            require(allocation["read_only"] == "false", "DSTORE into read-only allocation")
            writable.update(range(first, first + count))

    image = bytearray(files["image"].read_bytes())
    require(len(image) == config["used_lines"] * 256, "invalid initial image size")
    golden_rows = read_rows(files["golden_manifest"])
    expected_order = [(f"step_{step}", component, modulus_id)
                      for step, moduli in enumerate(spec["golden_moduli"])
                      for component in range(2)
                      for modulus_id in range(len(moduli))]
    actual_order = [(row["object_id"], int(row["component"]), int(row["modulus_id"]))
                    for row in golden_rows]
    require(actual_order == expected_order, "unexpected golden object order/shape")
    golden = bytearray()
    outputs = []
    allocations_by_span = {(int(row["line_offset"]), int(row["line_count"])): row
                           for row in allocations}
    for row in golden_rows:
        first, count = int(row["line_offset"]), int(row["line_count"])
        payload, padded = int(row["payload_words"]), int(row["padded_words"])
        modulus = int(row["modulus"])
        allocation = allocations_by_span.get((first, count))
        require(allocation is not None and allocation["kind"] == "output" and
                allocation["read_only"] == "false" and
                first_access.get(allocation["allocation_id"]) == "dstore",
                "golden span is not a write-first output")
        step = int(row["object_id"].split("_")[1])
        step_moduli = spec["golden_moduli"][step]
        require(row["domain"] == spec["golden_domain"] and
                count == 2 and payload == 128 and padded == 128 and
                modulus == step_moduli[int(row["modulus_id"])],
                "invalid golden limb dimensions/domain")
        data = safe_file(source, row["path"]).read_bytes()
        require(len(data) == padded * 4, "invalid golden limb length")
        values = struct.unpack(f"<{padded}I", data)
        require(all(value < modulus for value in values), "non-canonical golden residue")
        offset = len(golden) // 4
        golden.extend(data)
        outputs.append((first, count, padded, offset, modulus,
                        step,
                        int(row["component"]), int(row["modulus_id"])))
        image[first * 256:(first + count) * 256] = \
            struct.pack("<I", 0xDEADBEEF) * (count * WORDS_PER_LINE)
    require(len(outputs) == oracle["verified_limb_count"], "oracle/golden limb mismatch")
    image.extend(struct.pack("<I", 0xA5A55A5A) * (GUARD_LINES * WORDS_PER_LINE))
    mask = bytes(int(line in writable) for line in range(config["used_lines"] + GUARD_LINES))
    return commit, config, image, bytes(golden), mask, outputs


def import_package(source, destination, validator, producer_commit):
    commit, config, image, golden, mask, outputs = validate(
        source, validator, producer_commit)
    case = load_json(source / "package.json")["case_name"]
    spec = CASE_SPECS[case]
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as tmp:
        root = Path(tmp) / destination.name
        root.mkdir()
        for key in ("c", "h", "asm", "inst32", "cmd26"):
            shutil.copy2(source / "program" / f"{case}.{key}", root)
        shutil.copytree(source, root / "upstream")
        (root / "application_window.u32.bin").write_bytes(image)
        (root / "application_golden.u32.bin").write_bytes(golden)
        (root / "application_writable.u8.bin").write_bytes(mask)
        (root / "producer_commit.txt").write_text(commit + "\n", encoding="ascii")
        declarations = "\n".join(
            "    {%dU, %dU, %dU, %dU, %dU, %dU, %dU, %dU}," % row
            for row in outputs)
        (root / "application_layout.h").write_text(
            "#ifndef HPU_APPLICATION_LAYOUT_H\n#define HPU_APPLICATION_LAYOUT_H\n"
            f"enum {{ HPU_APPLICATION_IMAGE_LINES = {config['used_lines']}U,\n"
            f"       HPU_APPLICATION_CAPACITY_LINES = {config['capacity_lines']}U,\n"
            f"       HPU_APPLICATION_GUARD_LINES = {GUARD_LINES}U,\n"
            f"       HPU_APPLICATION_LINES = {len(mask)}U,\n"
            f"       HPU_APPLICATION_GOLDEN_COUNT = {len(outputs)}U,\n"
            f"       HPU_APPLICATION_GOLDEN_WORDS = {len(golden) // 4}U,\n"
            f"       HPU_APPLICATION_INSTRUCTION_COUNT = {spec['instruction_count']}U,\n"
            f"       HPU_APPLICATION_DMA_COUNT = {spec['dma_count']}U }};\n"
            "struct hpu_application_output {\n"
            "    unsigned line, lines, padded_words, golden_word, modulus;\n"
            "    unsigned step, component, modulus_id;\n};\n"
            "static const struct hpu_application_output\n"
            "hpu_application_outputs[HPU_APPLICATION_GOLDEN_COUNT] = {\n" +
            declarations + "\n};\n#endif\n", encoding="utf-8")
        if destination.exists():
            shutil.rmtree(destination)
        root.replace(destination)
    print(f"{spec['label']}: lines={len(mask)}, golden_limbs={len(outputs)}, "
          f"instructions={spec['instruction_count']}, dma={spec['dma_count']}, "
          f"commit={commit}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--producer-commit", required=True)
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit) is not None,
            "invalid producer commit")
    require(not args.destination.resolve().is_relative_to(args.source.resolve()) and
            not args.source.resolve().is_relative_to(args.destination.resolve()),
            "source and destination overlap")
    import_package(args.source, args.destination, args.validator, args.producer_commit)


if __name__ == "__main__":
    main()
