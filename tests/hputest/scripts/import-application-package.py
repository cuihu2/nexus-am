#!/usr/bin/env python3
"""接收 main v1：SEAL golden、scheme 物理布局及真实指令复放门禁。"""
import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from program_adaptation import adapt_bfv_image, adapt_ckks_program, coefficients_to_physical

ROOT = Path(__file__).resolve().parents[1]
GUARD_LINES = 64
WORDS_PER_LINE = 64
with (ROOT / "scheme-cases.tsv").open() as stream:
    CASE_SPECS = {r["program_stem"]: r for r in csv.DictReader(stream, delimiter="\t")}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read_rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def safe_file(root, relative):
    relative = Path(relative)
    require(not relative.is_absolute() and ".." not in relative.parts, "unsafe package path")
    path = (root / relative).resolve()
    require(path.is_relative_to(root.resolve()) and path.is_file(),
            f"missing package artifact: {relative}")
    return path


def load_json(path):
    return json.loads(path.read_text())


def package_files(source):
    package = load_json(source / "package.json")
    referenced = ("parameters", "operation_graph", "program_source", "program_header",
                  "program_asm", "program_inst32", "program_cmd26", "dma_relocation_manifest",
                  "image", "line_map", "memory_manifest", "memory_abi", "memory_config",
                  "golden_manifest", "oracle_report")
    return package, {key: safe_file(source, package[key]) for key in referenced}


def validate_seal(source, spec, producer_commit):
    if not spec["program_stem"].startswith("seal_"):
        return None
    report = load_json(source.with_name(source.name + ".seal.json"))
    api = {"hadd": "add", "hmul": "multiply+relinearize", "reline": "relinearize",
           "rotate": "rotate_vector" if spec["scheme"] == "ckks" else "rotate_rows",
           "modswitch": "rescale_to_next" if spec["scheme"] == "ckks" else "mod_switch_to_next"}[spec["role"]]
    require(bool(producer_commit) and report.get("producer_commit") == producer_commit and
            re.fullmatch(r"[0-9a-f]{40}", producer_commit) and
            report.get("library") == "inline-asm/third_party/modified-SEAL" and
            report.get("program") == spec["program_stem"] and
            report.get("scheme") == spec["scheme"] and report.get("degree") == int(spec["degree"]) and
            report.get("api") == f"seal::Evaluator::{api}" and report.get("status") == "PASS" and
            report.get("comparison") == "all raw physical words" and
            type(report.get("raw_word_mismatches")) is int and report["raw_word_mismatches"] == 0 and
            report.get("initial_correction_factors") == [1, 1] and report.get("rotation_generator") == 3 and
            report.get("rotation_steps") == (int(spec["degree"]) // 4 if spec["role"] == "rotate" else 0),
            "modified-SEAL oracle/provenance mismatch")
    return report


def validate(source, validator, producer_commit=None):
    subprocess.run([str(validator), str(source)], check=True)
    package, files = package_files(source)
    case = package.get("case_name")
    require(case in CASE_SPECS, f"unsupported application case: {case}")
    spec = CASE_SPECS[case]
    validate_seal(source, spec, producer_commit)
    require(package.get("schema") == "hpu-application-package" and
            package.get("schema_version") == 1 and package.get("scheme") == spec["scheme"] and
            package.get("word_bits") == 32 and package.get("line_words") == 64 and
            package.get("line_bytes") == 256 and package.get("byte_order") == "little-endian",
            "application schema/ABI mismatch")
    build = load_json(source / "provenance/build.json")
    commit = build.get("commit", "")
    require(re.fullmatch(r"[0-9a-f]{40}", commit) and
            (producer_commit is None or commit == producer_commit) and
            build.get("worktree_state") == "clean-at-configure", "producer provenance mismatch")
    oracle = load_json(files["oracle_report"])
    model = {"ckks": "CkksSoftwareExecutor", "bfv": "BfvSoftwareExecutor",
             "bgv": "BgvSoftwareExecutor"}[spec["scheme"]]
    require(oracle.get("overall_status") == "pass" and
            oracle.get("oracle_verified") is True and oracle.get("golden_matches_oracle") is True and
            oracle.get("model_verified") is True and oracle.get("raw_physical_words_equal") is True and
            oracle.get("model") == model, "SEAL/software-model oracle did not pass")
    checks = {r["name"]: r for r in oracle.get("checks", [])}
    for name in ("seal_oracle_to_golden", "host_software_model_to_oracle"):
        require(checks.get(name, {}).get("status") == "pass" and
                checks[name].get("required") is True, f"missing required {name}")
    require(all(oracle.get(key) is False for key in
                ("instruction_execution_verified", "rtl_verified", "hardware_verified")),
            "upstream package must not claim target execution")
    parameters = load_json(files["parameters"])
    n = int(spec["degree"])
    require(parameters.get("scheme") == spec["scheme"] and
            parameters.get("poly_modulus_degree") == n, "scheme/degree mismatch")
    operations = load_json(files["operation_graph"])["operations"]
    role = spec["role"]
    kinds = [op["kind"] for op in operations]
    if role != "application":
        expected = {"hadd": ["add"], "keyswitch": ["relinearize"], "reline": ["relinearize"],
                    "modswitch": ["rescale" if spec["scheme"] == "ckks" else "mod_switch"],
                    "rotate": ["rotate" if spec["scheme"] == "ckks" else "rotate_rows"]}
        if role == "hmul":
            require(kinds in (["multiply", "relinearize"], ["multiply_relinearize"], ["multiply"]),
                    "HMUL graph mismatch")
        else:
            require(kinds == expected[role], "standalone operation graph mismatch")
        require(operations[-1]["component_count"] == 2, "operator final component count")
    require([r.get("operation_index", i) for i, r in enumerate(operations)] ==
            list(range(len(operations))), "operation indices are not contiguous")
    config = load_json(files["memory_config"])
    require(0 < config["used_lines"] <= config["capacity_lines"], "window geometry")
    abi = load_json(files["memory_abi"])
    require(abi.get("rs1") == "x10" and abi.get("rs2") == "x11" and
            abi.get("line_bytes") == 256, "DMA ABI mismatch")
    words = [int(w, 2) for w in files["program_inst32"].read_text().split()]
    code = files["program_source"].read_text()
    require([int(w, 16) for w in re.findall(r'\.word (0x[0-9A-Fa-f]+)', code)] == words,
            "C/INST32 mismatch")
    require(words and words[-1] == 0x7000005B and words.count(0x7000005B) == 1 and
            all(w & 0x7F in (0x2B, 0x5B) for w in words), "opcode/terminal PSYNC mismatch")
    allocations = read_rows(files["line_map"])
    by_id = {r["allocation_id"]: r for r in allocations}
    require(len(by_id) == len(allocations), "duplicate allocation")
    manifest = read_rows(files["memory_manifest"])
    fields = ("allocation_id", "kind", "read_only", "line_offset", "line_count",
              "payload_words", "padded_words", "initialization")
    require([{key: r[key] for key in fields} for r in allocations] ==
            [{key: r[key] for key in fields} for r in manifest], "memory manifests differ")
    previous = 0
    for row in sorted(allocations, key=lambda r: int(r["line_offset"])):
        first, lines = int(row["line_offset"]), int(row["line_count"])
        require(previous <= first and lines > 0 and first + lines <= config["used_lines"] and
                int(row["padded_words"]) == lines * 64, "allocation overlap/shape")
        previous = first + lines
    dma = read_rows(files["dma_relocation_manifest"])
    spans = [(int(a), int(b)) for a, b in re.findall(
        r'\{ UINT32_C\((\d+)\), UINT32_C\((\d+)\) \}', code)]
    require(spans == [(int(r["line_offset"]), int(r["line_count"])) for r in dma],
            "C resolved spans differ from DMA manifest")
    require([int(r["instruction_index"]) for r in dma] ==
            [i for i, w in enumerate(words) if w & 0x7F == 0x2B], "DMA order mismatch")
    writable, first_access = set(), {}
    for index, row in enumerate(dma):
        allocation = by_id.get(row["allocation_id"])
        require(allocation is not None and int(row["dma_index"]) == index, "DMA allocation/index")
        first, count = int(row["line_offset"]), int(row["line_count"])
        require((first, count) == (int(allocation["line_offset"]), int(allocation["line_count"])) and
                words[int(row["instruction_index"])] == int(row["word_hex"], 16), "DMA span/word")
        first_access.setdefault(row["allocation_id"], row["direction"])
        if row["direction"] == "dstore":
            require(allocation["read_only"] == "false", "DSTORE into readonly allocation")
            writable.update(range(first, first + count))
        op = row["operation_index"]
        require((not op and row["operation_id"] == "$application") or
                (op and 0 <= int(op) < len(operations) and
                 operations[int(op)]["id"] == row["operation_id"]), "DMA graph provenance")
    image = bytearray(files["image"].read_bytes())
    require(len(image) == config["used_lines"] * 256, "image byte count")
    if spec["scheme"] == "bfv":
        adapt_bfv_image(image, allocations, n)
    golden_rows = read_rows(files["golden_manifest"])
    levels = {tuple(level["parms_id"]): level["moduli"] for level in parameters["levels"]}
    by_span = {(int(r["line_offset"]), int(r["line_count"])): r for r in allocations}
    golden, outputs, order = bytearray(), [], []
    domain = "coefficient" if spec["scheme"] == "bfv" else "canonical_ntt_physical"
    for row in golden_rows:
        step = int(row["object_id"].removeprefix("step_"))
        op = operations[step]
        moduli = levels[tuple(op["parms_id"])]
        component, modulus_id = int(row["component"]), int(row["modulus_id"])
        first, lines = int(row["line_offset"]), int(row["line_count"])
        padded = int(row["padded_words"])
        allocation = by_span.get((first, lines))
        require(allocation is not None and allocation["kind"] == "output" and
                first_access.get(allocation["allocation_id"]) == "dstore", "golden not write-first output")
        require(component < op["component_count"] and modulus_id < len(moduli) and
                int(row["modulus"]) == moduli[modulus_id] and row["domain"] == domain and
                int(row["payload_words"]) == n and padded == lines * 64 == n, "golden limb shape/domain")
        data = safe_file(source, row["path"]).read_bytes()
        require(len(data) == padded * 4 and
                all(v < moduli[modulus_id] for v in struct.unpack(f"<{padded}I", data)),
                "non-canonical or truncated SEAL golden")
        if spec["scheme"] == "bfv":
            data = coefficients_to_physical(data, n)
        outputs.append((first, lines, padded, len(golden) // 4, moduli[modulus_id],
                        step, component, modulus_id))
        order.append((step, component, modulus_id))
        golden += data
        image[first*256:(first+lines)*256] = b"\xff" * (lines*256)
    expected = [(s, c, q) for s, op in enumerate(operations)
                for c in range(op["component_count"]) for q in range(len(levels[tuple(op["parms_id"])]))]
    require(order == expected and len(outputs) == oracle["verified_limb_count"], "golden coverage")
    return commit, config, image, bytes(golden), writable, outputs


def import_package(source, destination, validator, producer_commit, encoder, program_model):
    commit, config, image, golden, writable, outputs = validate(source, validator, producer_commit)
    package, files = package_files(source)
    stem = package["case_name"]
    spec = CASE_SPECS[stem]
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as temporary:
        root = Path(temporary) / destination.name
        root.mkdir()
        adapted = None
        allocations = read_rows(files["line_map"])
        parameters = load_json(files["parameters"])
        if spec["scheme"] == "ckks":
            adapted = adapt_ckks_program(source, files, image, allocations, parameters, encoder, root)
        for key, extension in (("program_source", "c"), ("program_header", "h"),
                               ("program_asm", "asm"), ("program_inst32", "inst32"),
                               ("program_cmd26", "cmd26")):
            shutil.copy2(files[key], root / f"{stem}.{extension}")
        dma = read_rows(files["dma_relocation_manifest"])
        if adapted:
            dma = adapted["dma"]
            writable = set()
            for row in dma:
                if row["direction"] == "dstore":
                    writable.update(range(int(row["line_offset"]),
                                          int(row["line_offset"]) + int(row["line_count"])))
            (root / f"{stem}.c").write_text(adapted["code"])
            (root / f"{stem}.h").write_text(adapted["header"])
            (root / f"{stem}.asm").write_text(adapted["assembly"])
            for field, bits, extension in (("word_hex", 32, "inst32"), ("command_hex", 26, "cmd26")):
                (root / f"{stem}.{extension}").write_text(
                    "\n".join(format(int(r[field], 16), f"0{bits}b") for r in adapted["encoded"]) + "\n")
        # 在每条 DMA 发射前保留指令位置；silent 版也能通过 DDR/PC 定位反压停点。
        code_path = root / f"{stem}.c"
        c_source = code_path.read_text()
        c_source = '#include <hpu/trace.h>\n' + c_source
        for row in dma:
            index = int(row["instruction_index"])
            marker = re.compile(r"(    /\* " + str(index) + r": [^\n]+ \*/\n)")
            c_source, count = marker.subn(
                lambda match: match[1] +
                f"    trace_issue({index}U, {row['dma_index']}U, {row['word_hex']}U);\n",
                c_source)
            require(count == 1, "missing DMA trace insertion marker")
        code_path.write_text(c_source)
        with (root / "dma_relocation_manifest.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(dma[0]))
            writer.writeheader(); writer.writerows(dma)
        used = len(image) // 256
        config = {"used_lines": used, "capacity_lines": used}
        image += struct.pack("<I", 0xA5A55A5A) * (64 * GUARD_LINES)
        mask = bytes(int(line in writable) for line in range(used + GUARD_LINES))
        (root / "application_window.u32.bin").write_bytes(image)
        (root / "application_golden.u32.bin").write_bytes(golden)
        (root / "application_writable.u8.bin").write_bytes(mask)
        shutil.copytree(source, root / "upstream")
        (root / "producer_commit.txt").write_text(commit + "\n")
        model_rows = []
        for first, lines, padded, offset, modulus, step, component, modulus_id in outputs:
            filename = f"model-golden-{len(model_rows)}.u32.bin"
            (root / filename).write_bytes(golden[offset*4:(offset+padded)*4])
            model_rows.append({"object_id": f"step_{step}", "component": component,
                               "modulus_id": modulus_id, "path": filename, "line_offset": first})
        with (root / "model_golden.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(model_rows[0]))
            writer.writeheader(); writer.writerows(model_rows)
        model_image = root / "program_model_result.u32.bin"
        with (root / "PROGRAM_MODEL.log").open("w") as log:
            result = subprocess.run([str(program_model), str(root / f"{stem}.asm"),
                            str(root / "application_window.u32.bin"),
                            str(root / "dma_relocation_manifest.csv"), str(root / "model_golden.csv"),
                            str(root), str(model_image)], stdout=log, stderr=log)
        if result.returncode != 0:
            raise ValueError((root / "PROGRAM_MODEL.log").read_text())
        actual = model_image.read_bytes()
        require(all(actual[line*256:(line+1)*256] == image[line*256:(line+1)*256]
                    for line in range(len(mask)) if not mask[line]), "program model readonly/guard changed")
        model_image.unlink()
        for row in model_rows: (root / row["path"]).unlink()
        (root / "model_golden.csv").unlink()
        adaptation = {"producer_commit": commit, "golden_source": "modified-SEAL Evaluator",
                      "bfv_coefficient_permutation": spec["scheme"] == "bfv",
                      "ckks_rounded_p_instructions_added": adapted["instructions_added"] if adapted else 0,
                      "ckks_input_shadow_count": adapted["input_shadow_count"] if adapted else 0,
                      "program_model_matches_seal": True, "rtl_verified": False}
        if stem.startswith("seal_"):
            sidecar = source.with_name(source.name + ".seal.json")
            adaptation["seal_oracle"] = load_json(sidecar)
            shutil.copy2(sidecar, root / "SEAL_ORACLE.json")
        (root / "AM_ADAPTATION.json").write_text(json.dumps(adaptation, indent=2) + "\n")
        declarations = "\n".join("    {%dU, %dU, %dU, %dU, %dU, %dU, %dU, %dU}," % row
                                 for row in outputs)
        instructions = len((root / f"{stem}.inst32").read_text().split())
        (root / "application_layout.h").write_text(
            "#ifndef HPU_APPLICATION_LAYOUT_H\n#define HPU_APPLICATION_LAYOUT_H\n"
            f"enum {{ HPU_APPLICATION_IMAGE_LINES = {used}U,\n"
            f" HPU_APPLICATION_CAPACITY_LINES = {used}U, HPU_APPLICATION_LINES = {len(mask)}U,\n"
            f" HPU_APPLICATION_GUARD_LINES = {GUARD_LINES}U,\n"
            f" HPU_APPLICATION_GOLDEN_COUNT = {len(outputs)}U,\n"
            f" HPU_APPLICATION_GOLDEN_WORDS = {len(golden)//4}U,\n"
            f" HPU_APPLICATION_INSTRUCTION_COUNT = {instructions}U,\n"
            f" HPU_APPLICATION_DMA_COUNT = {len(dma)}U }};\n"
            "struct hpu_application_output { unsigned line, lines, padded_words, golden_word, modulus;\n"
            " unsigned step, component, modulus_id; };\n"
            "static const struct hpu_application_output hpu_application_outputs[] = {\n" +
            declarations + "\n};\n#endif\n")
        if destination.exists(): shutil.rmtree(destination)
        root.replace(destination)
    print(f"{spec['case_id']}: lines={used}, SEAL_limbs={len(outputs)}, commands={instructions}, DMA={len(dma)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "destination", "validator", "encoder", "program-model"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--producer-commit", required=True)
    args = parser.parse_args()
    require(not args.destination.resolve().is_relative_to(args.source.resolve()) and
            not args.source.resolve().is_relative_to(args.destination.resolve()), "overlapping import")
    import_package(args.source, args.destination, args.validator, args.producer_commit,
                   args.encoder, args.program_model)
