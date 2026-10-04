"""AM 的显式物理布局/rounded-P 适配；原始交付始终保留，不修改 SEAL 数学 golden。"""
import csv
import json
import re
import struct
import subprocess
from pathlib import Path


def coefficients_to_physical(data, degree):
    values = struct.unpack(f"<{degree}I", data)
    bits = degree.bit_length() - 1
    physical = [values[int(f"{i:0{bits}b}"[::-1], 2)] for i in range(degree)]
    return struct.pack(f"<{degree}I", *physical)


def adapt_bfv_image(image, allocations, degree):
    for allocation in allocations:
        # BFV ciphertext/plaintext 是 SEAL 自然系数顺序；NTT/key/twiddle 已是物理域。
        if allocation["kind"] not in ("ciphertext", "plaintext"):
            continue
        first = int(allocation["line_offset"]) * 256
        if int(allocation["payload_words"]) != degree:
            raise ValueError("BFV coefficient allocation shape changed")
        image[first:first + degree * 4] = coefficients_to_physical(
            image[first:first + degree * 4], degree)


def _new_c(asm, word, dma_index=None):
    prefix = ""
    if asm.startswith("dload"):
        prefix = f"    hpu_obj_len[0] = spans[{dma_index}].line_count;\n"
    elif asm.startswith("dstore"):
        prefix = (f"    if (hpu_obj_len[0] != spans[{dma_index}].line_count) return -4;\n")
    suffix = "    hpu_obj_len[0] = 0;\n" if asm.startswith("dstore") else ""
    if asm == "pfree p1": suffix = "    hpu_obj_len[1] = 0;\n"
    if asm.startswith("dload") and ", p1," in asm:
        prefix = f"    hpu_obj_len[1] = spans[{dma_index}].line_count;\n"
    if dma_index is None:
        body = f'    __asm__ volatile(".word {word}" : : : "memory");\n'
    else:
        body = ("    {\n"
                f'        register uintptr_t hpu_rs1 __asm__("x10") = spans[{dma_index}].line_offset;\n'
                f'        register uintptr_t hpu_rs2 __asm__("x11") = spans[{dma_index}].line_count;\n'
                f'        __asm__ volatile(".word {word}" : : "r"(hpu_rs1), "r"(hpu_rs2) : "memory");\n'
                "    }\n")
    return prefix + "#if defined(__riscv)\n" + body + "#endif\n" + suffix


def adapt_ckks_program(source, files, image, allocations, parameters, encoder, destination):
    code = files["program_source"].read_text()
    comments = list(re.finditer(r"    /\* (\d+): ([^\n]+) \*/\n", code))
    if not comments or [int(m[1]) for m in comments] != list(range(len(comments))):
        raise ValueError("producer C instruction markers changed")
    end = code.index("\n    return 0;", comments[-1].end())
    chunks = [code[m.end():(comments[i+1].start() if i+1<len(comments) else end)]
              for i, m in enumerate(comments)]
    dma = list(csv.DictReader(files["dma_relocation_manifest"].open()))
    operations = json.loads(files["operation_graph"].read_text())["operations"]
    shadows = {}
    diverted = set()
    by_id = {row["allocation_id"]: row for row in allocations}
    for binding in dma:
        if not binding["operation_index"]:
            continue
        operation = operations[int(binding["operation_index"])]
        name = binding["allocation_id"]
        if operation["kind"] not in ("relinearize", "rescale") or not any(
                name.startswith(value + "/") for value in operation["inputs"]):
            continue
        key = (binding["operation_index"], name)
        if binding["direction"] == "dstore":
            if key not in shadows:
                original = by_id[name]
                first = len(image) // 256
                image += b"\0" * (int(original["line_count"]) * 256)
                shadows[key] = {**original, "allocation_id": f"am/shadow/step{key[0]}/{name}",
                                "kind": "workspace", "read_only": "false",
                                "line_offset": str(first), "initialization": "zero_reserved"}
                allocations.append(shadows[key])
            diverted.add(key)
        if key in diverted:
            # 首次 INTT 仍读原 NTT；其系数写回及后续系数读取走私有 workspace。
            # 这样已有的逐算子 NTT golden 与后续分支输入不会被原地破坏。
            binding.update({field: shadows[key][field] for field in
                            ("allocation_id", "line_offset", "line_count")})
    by_pc = {int(row["instruction_index"]): row for row in dma}
    insertions = {}
    pc = 0
    for line in files["program_asm"].read_text().splitlines():
        if "Step 5: ModDown for both parts" in line:
            insertions[pc] = []
        if re.search(r'"(\w+ [^\"]*|psync)\s*\\n\\t"', line) or re.match(
                r"^(dload|dstore|padd|psub|pmul|pmac|pntt|pintt|pmodld|pfree|psync)\b", line.strip()):
            pc += 1
    if pc != len(comments):
        raise ValueError("ASM/C instruction count mismatch")
    if not insertions and not shadows:
        return None
    n = parameters["poly_modulus_degree"]
    moduli = parameters["key_moduli"]
    p_id = len(moduli) - 1
    half = moduli[p_id] // 2
    half_spans = {}
    for position in insertions:
        next_dma = next(row for row in dma if int(row["instruction_index"]) >= position)
        name = next_dma["allocation_id"]
        if "/workspace/accumulator/" not in name:
            raise ValueError("rounded-P insertion no longer begins on an accumulator")
        prefix = name.split("/workspace/accumulator/")[0] + "/workspace/accumulator/"
        accumulators = [row for row in allocations if row["allocation_id"].startswith(prefix)]
        ids = sorted({int(row["allocation_id"].rsplit("mod",1)[1]) for row in accumulators})
        if p_id not in ids or len(accumulators) != 2 * len(ids):
            raise ValueError("single-P accumulator layout mismatch")
        for component in range(2):
            for modulus_id in ids:
                target = next(row for row in accumulators
                              if row["allocation_id"] == prefix + f"c{component}/mod{modulus_id}")
                if modulus_id not in half_spans:
                    first = len(image) // 256
                    image += struct.pack("<I", half % moduli[modulus_id]) * n
                    half_spans[modulus_id] = {
                        "allocation_id": f"am/rounded_p/half/mod{modulus_id}", "kind": "constant",
                        "read_only": "true", "line_offset": str(first), "line_count": str(n//64),
                        "payload_words": str(n), "padded_words": str(n), "initialization": "payload"}
                    allocations.append(half_spans[modulus_id])
                context = {key: next_dma[key] for key in
                           ("operation_index", "operation_id", "operation_dma_index")}
                for asm, allocation, direction, slot, kind in (
                        (f"pmodld {modulus_id}", None, None, None, None),
                        ("dload x10, x11, p0, 1, 0", target, "dload", 0, 1),
                        ("dload x10, x11, p1, 1, 0", half_spans[modulus_id], "dload", 1, 1),
                        ("padd p0, p0, p1", None, None, None, None),
                        ("pfree p1", None, None, None, None),
                        ("dstore x10, x11, p0, 1", target, "dstore", 0, 1)):
                    binding = None
                    if allocation is not None:
                        binding = {**context, "direction": direction, "object_slot": str(slot),
                                   "type_or_release": str(kind), "flag": "0",
                                   **{key: allocation[key] for key in ("allocation_id","line_offset","line_count")}}
                    insertions[position].append({"asm": asm, "dma": binding, "origin": None})
    sequence = []
    for index,m in enumerate(comments):
        sequence.extend(insertions.get(index, []))
        sequence.append({"asm": m[2], "dma": by_pc.get(index), "origin": index})
    assembly_path = destination / "adapted.asm"
    table_path = destination / "adapted_encodings.tsv"
    assembly_path.write_text("\n".join(row["asm"] for row in sequence) + "\n")
    subprocess.run([str(encoder), str(assembly_path), str(table_path)], check=True)
    with table_path.open() as stream:
        encoded = list(csv.DictReader(stream, delimiter="\t"))
    if len(encoded) != len(sequence): raise ValueError("adapted encoder count mismatch")
    new_dma, bodies, operation_counts = [], [], {}
    for index,(entry,encoding) in enumerate(zip(sequence,encoded)):
        binding = entry["dma"]
        dma_index = None
        if binding is not None:
            dma_index = len(new_dma)
            op = binding["operation_id"]
            internal = operation_counts.get(op,0); operation_counts[op] = internal + 1
            new_dma.append({**binding, "instruction_index": str(index), "dma_index": str(dma_index),
                            "operation_dma_index": str(internal), "word_hex": encoding["word_hex"],
                            "normalized_asm": encoding["normalized_asm"]})
        if entry["origin"] is None:
            body = _new_c(entry["asm"],encoding["word_hex"],dma_index)
        else:
            body = chunks[entry["origin"]]
            if dma_index is not None:
                body = re.sub(r"spans\[\d+\]", f"spans[{dma_index}]", body)
        bodies.append(f"    /* {index}: {encoding['normalized_asm']} */\n" + body)
    result_code = code[:comments[0].start()] + "".join(bodies) + code[end:]
    spans = "\n".join(f"    {{ UINT32_C({row['line_offset']}), UINT32_C({row['line_count']}) }},"
                      for row in new_dma)
    result_code,n_replaced = re.subn(
        r"(static const hpu_dma_span_t \w+_resolved_spans\[\] = \{)\n.*?\n};",
        lambda match: match[1] + "\n" + spans + "\n};", result_code, flags=re.S)
    if n_replaced != 1: raise ValueError("resolved span declaration changed")
    result_code = re.sub(r"#define HPU_MEM_LINE_COUNT UINT64_C\(\d+\)",
                         f"#define HPU_MEM_LINE_COUNT UINT64_C({len(image)//256})", result_code)
    header = files["program_header"].read_text()
    header,n_replaced = re.subn(r"(HPU_PROGRAM_\w+_DMA_COUNT\s*=\s*)\d+",
                               lambda match: match[1]+str(len(new_dma)), header)
    if n_replaced != 1: raise ValueError("DMA count declaration changed")
    return {"code": result_code, "header": header, "dma": new_dma, "encoded": encoded,
            "instructions_added": len(sequence)-len(comments),
            "input_shadow_count": len(shadows),
            "assembly": "\n".join(entry["normalized_asm"] for entry in encoded)+"\n"}
