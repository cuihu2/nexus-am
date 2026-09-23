#!/usr/bin/env python3
"""导入固定 Q4/P3/D2 KeySwitch，并在 AM 侧解析 716 条 DMA 绑定。"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import tempfile

from hpu_ntt_layout import HARDWARE_LAYOUT, bit_reverse, forward_layout
from hpu_opcode_mapping import map_c, map_word
from program_encoding import check_program


N = 4096
LINE_BYTES = 256
WORDS_PER_LINE = LINE_BYTES // 4
POLY_LINES = N // WORDS_PER_LINE
MODULI = [50061313, 50077697, 50307073, 50552833, 90062849, 90095617, 90218497]
NUM_Q, NUM_P, DNUM = 4, 3, 2
TOTAL_QP, DIGIT_SIZE = NUM_Q + NUM_P, NUM_Q // DNUM
EXPECTED_INSTRUCTIONS = 2167
EXPECTED_DMA = 716
GUARD_LINES = 64

MATH_ARTIFACTS = {
    "input_base_q.bin": ((NUM_Q, N), False),
    "input_t2_q.bin": ((NUM_Q, N), False),
    "rlk_ntt_qp.bin": ((DNUM, 2, TOTAL_QP, N), True),
    "expected_q.bin": ((2, NUM_Q, N), False),
}
SUPPORT_PATHS = (
    "images/constants/modup_digit_qhat_inv.u32.bin",
    "images/constants/modup_digit_qhat_mod_qp.u32.bin",
    "images/constants/moddown_p_qhat_inv.u32.bin",
    "images/constants/moddown_p_qhat_mod_q.u32.bin",
    "images/constants/p_inverse_mod_q.u32.bin",
)
SUPPORT_LINES = (NUM_Q * POLY_LINES, DNUM * TOTAL_QP * DIGIT_SIZE * POLY_LINES,
                 NUM_P * POLY_LINES, NUM_Q * NUM_P * POLY_LINES,
                 NUM_Q * POLY_LINES)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def fnv1a64(data: bytes) -> int:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = value * 1099511628211 & 0xFFFFFFFFFFFFFFFF
    return value


def checksum(text: str) -> int:
    return int(text, 0)


def shape(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in text.split("x"))


def safe_relative(path: str) -> None:
    pure = PurePosixPath(path)
    require(path and "\\" not in path and not pure.is_absolute() and ".." not in pure.parts,
            f"unsafe producer-relative path: {path!r}")


def parse_program(path: Path, marker: str | None = None) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if marker is not None:
        matches = [index for index, line in enumerate(lines) if marker in line]
        require(len(matches) == 1, f"expected one {marker!r} marker in {path.name}")
        lines = lines[matches[0] + 1:]
    program = []
    for line in lines:
        match = re.search(r'"(.*?)\\n\\t"', line)
        if match:
            program.append(match.group(1).strip())
    return program


def validate_params(package: Path, operation: str) -> dict[str, object]:
    params = json.loads((package / "test_data/params.json").read_text(encoding="utf-8"))
    require(params.get("format_version") == 1 and params.get("operation") == operation,
            f"{operation}: wrong params format/operation")
    require(params.get("N") == N and params.get("moduli") == MODULI,
            f"{operation}: expected fixed N4096 Q4/P3 moduli")
    require(HARDWARE_LAYOUT in params.get("hardware_layout", ""),
            f"{operation}: obsolete/unknown hardware layout")
    return params


def validate_hardware(package: Path) -> dict[str, object]:
    hardware = package / "test_data/hardware"
    abi = json.loads((hardware / "abi.json").read_text(encoding="utf-8"))
    require(abi.get("format_version") == 1 and abi.get("N") == N
            and abi.get("modulus_count") == TOTAL_QP
            and abi.get("coefficient_bits") == 32
            and abi.get("byte_order") == "little-endian"
            and abi.get("line_bytes") == LINE_BYTES
            and abi.get("line_words") == WORDS_PER_LINE,
            f"{package.name}: incompatible hardware ABI")
    require(abi.get("twiddle_images_included") is True,
            f"{package.name}: KeySwitch support requires twiddle images")

    line_rows = rows(hardware / "line_map.csv")
    manifest_rows = rows(hardware / "hardware_manifest.csv")
    require(line_rows and manifest_rows, f"{package.name}: empty hardware manifests")
    manifest = {row["path"]: row for row in manifest_rows}
    require(len(manifest) == len(manifest_rows), f"{package.name}: duplicate hardware path")
    image = (hardware / "hpu_mem_image.u32.bin").read_bytes()
    config = json.loads((hardware / "hpu_mem_config.json").read_text(encoding="utf-8"))
    size_lines = int(config.get("size_lines", 0))
    require(size_lines > 0 and len(image) == size_lines * LINE_BYTES
            and int(config.get("size_bytes", 0)) == len(image),
            f"{package.name}: HPU_MEM image/config size mismatch")
    require(checksum(config.get("image_fnv1a64", "-1")) == fnv1a64(image),
            f"{package.name}: HPU_MEM config checksum mismatch")
    complete = manifest.get("hpu_mem_image.u32.bin")
    require(complete is not None
            and checksum(complete["payload_fnv1a64"]) == fnv1a64(image)
            and checksum(complete["image_fnv1a64"]) == fnv1a64(image),
            f"{package.name}: complete-image manifest checksum mismatch")

    catalog, blobs = {}, {}
    occupied = []
    for entry in line_rows:
        relative = entry["path"]
        safe_relative(relative)
        require(relative not in catalog and relative in manifest,
                f"{package.name}: duplicate or unmanifested line-map path: {relative}")
        offset, count = int(entry["line_offset"]), int(entry["line_count"])
        payload_words = int(entry["payload_words"])
        padded_words = int(entry["padded_words"])
        require(count > 0 and padded_words == count * WORDS_PER_LINE
                and int(entry["payload_bytes"]) == payload_words * 4
                and int(entry["padded_bytes"]) == padded_words * 4,
                f"{package.name}: invalid line geometry for {relative}")
        raw = (hardware / relative).read_bytes()
        require(len(raw) == count * LINE_BYTES,
                f"{package.name}: file size differs from line map: {relative}")
        item = manifest[relative]
        require(int(item["payload_words"]) == payload_words
                and int(item["padded_words"]) == padded_words
                and int(item["line_offset"]) == offset
                and int(item["line_count"]) == count,
                f"{package.name}: line/hardware manifest mismatch: {relative}")
        require(checksum(item["payload_fnv1a64"]) == fnv1a64(raw[:payload_words * 4])
                and checksum(item["image_fnv1a64"]) == fnv1a64(raw),
                f"{package.name}: hardware checksum mismatch: {relative}")
        start, end = offset * LINE_BYTES, (offset + count) * LINE_BYTES
        require(end <= len(image) and image[start:end] == raw,
                f"{package.name}: split image differs from HPU_MEM image: {relative}")
        catalog[relative] = (offset, count)
        blobs[relative] = raw
        occupied.append((offset, offset + count, relative))
    require(set(manifest) == set(catalog) | {"hpu_mem_image.u32.bin"},
            f"{package.name}: hardware manifest/line map coverage differs")
    occupied.sort()
    require(occupied[0][0] == 0 and all(left[1] == right[0]
            for left, right in zip(occupied, occupied[1:]))
            and occupied[-1][1] == size_lines,
            f"{package.name}: HPU_MEM line map is not contiguous")
    return {"hardware": hardware, "abi": abi, "config": config, "image": image,
            "catalog": catalog, "blobs": blobs, "line_rows": line_rows}


def validate_math_and_layout(package: Path, hardware: dict[str, object]) -> None:
    data = package / "test_data"
    manifest_rows = rows(data / "artifact_manifest.csv")
    manifest = {row["path"]: row for row in manifest_rows}
    require(set(manifest) == set(MATH_ARTIFACTS),
            "keyswitch: unexpected mathematical artifact set")
    coefficient_layout = [bit_reverse(index, N) for index in range(N)]
    ntt_layout = forward_layout(N)
    for relative, (expected_shape, ntt_domain) in MATH_ARTIFACTS.items():
        entry = manifest[relative]
        raw = (data / relative).read_bytes()
        require(shape(entry["shape"]) == expected_shape
                and int(entry["elements"]) == len(raw) // 8
                and int(entry["bytes"]) == len(raw)
                and entry["hardware_visible"] == "1"
                and checksum(entry["fnv1a64"]) == fnv1a64(raw),
                f"keyswitch: math manifest/checksum mismatch: {relative}")
        words = struct.unpack(f"<{len(raw) // 8}Q", raw)
        require(all(word <= 0xFFFFFFFF for word in words),
                f"keyswitch: value does not fit uint32: {relative}")
        layout = ntt_layout if ntt_domain else coefficient_layout
        physical = []
        for base in range(0, len(words), N):
            physical.extend(words[base + index] for index in layout)
        hardware_path = f"images/{Path(relative).stem}.u32.bin"
        expected = struct.pack(f"<{len(physical)}I", *physical)
        require(hardware["blobs"].get(hardware_path) == expected,
                f"keyswitch: math/hardware layout mismatch: {relative}")


def validate_mod_context(hardware: dict[str, object]) -> None:
    raw = hardware["blobs"]["constants/mod_ctx.u32.bin"]
    words = struct.unpack(f"<{len(raw) // 4}I", raw)
    expected = []
    for modulus in MODULI:
        mu = (1 << 64) // modulus
        expected += [modulus, mu & 0xFFFFFFFF, mu >> 32, 0]
    require(list(words[:len(expected)]) == expected and not any(words[len(expected):]),
            "keyswitch: invalid q32/mu48 modulus-context table")


def validate_program(package: Path, encodings: Path) -> tuple[list[str], list[int], list[dict[str, str]]]:
    program = parse_program(package / "keyswitch.asm")
    require(len(program) == EXPECTED_INSTRUCTIONS and program[-1] == "psync"
            and program.count("psync") == 1,
            "keyswitch: instruction count or terminal PSYNC contract changed")
    words = [int(bits, 2) for bits in (package / "keyswitch.inst32").read_text().split()]
    commands = [int(bits, 2) for bits in (package / "keyswitch.cmd26").read_text().split()]
    source = (package / "keyswitch.c").read_text(encoding="utf-8")
    source_words = [int(word, 16) for word in re.findall(r"\.word 0x([0-9A-Fa-f]{8})", source)]
    source_program = [asm for _, asm in re.findall(r"/\* (\d+): (.*?) \*/", source)]
    require(source_program == program and source_words == words,
            "keyswitch: C comments/words differ from ASM/inst32")
    require(commands == [(word >> 7) | ((1 << 25) if word & 0x7F == 0x2B else 0)
                         for word in words], "keyswitch: cmd26 differs from inst32")
    check_program(program, words, encodings)

    relocations = rows(package / "dma_relocation_manifest.csv")
    dma_indices = [index for index, asm in enumerate(program)
                   if asm.startswith(("dload", "dstore"))]
    require(len(relocations) == len(dma_indices) == EXPECTED_DMA,
            "keyswitch: expected 716 DMA relocations")
    for dma, (entry, instruction) in enumerate(zip(relocations, dma_indices)):
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
                f"keyswitch: relocation {dma} differs from encoded program")
    return program, words, relocations


class Planner:
    def __init__(self, catalog: dict[str, tuple[int, int]]):
        self.catalog = catalog
        self.records: list[dict[str, object]] = []

    def whole(self, artifact: str, logical: str) -> tuple[str, str, int, int]:
        require(artifact in self.catalog, f"planned artifact absent from line map: {artifact}")
        offset, count = self.catalog[artifact]
        return logical, artifact, offset, count

    def poly(self, artifact: str, index: int, logical: str) -> tuple[str, str, int, int]:
        name, path, offset, count = self.whole(artifact, logical)
        start = index * POLY_LINES
        require(start + POLY_LINES <= count, f"planned polynomial exceeds artifact: {artifact}")
        return name, path, offset + start, POLY_LINES

    def polys(self, artifact: str, first: int, count: int, label: str):
        return [self.poly(artifact, first + index, f"{label}[{index}]") for index in range(count)]

    def add(self, direction: str, slot: int, span) -> None:
        logical, artifact, offset, count = span
        self.records.append({"direction": direction, "object_slot": slot,
                             "logical_object": logical, "artifact": artifact,
                             "line_offset": offset, "line_count": count, "status": "RESOLVED"})

    def load(self, slot: int, span) -> None: self.add("dload", slot, span)
    def store(self, slot: int, span) -> None: self.add("dstore", slot, span)
    def load_mod(self) -> None: self.load(4, self.whole("constants/mod_ctx.u32.bin", "mod_context_table"))

    def bconv(self, source, inverse, target_constants, normalized, targets) -> None:
        require(len(source) == len(inverse) == len(normalized)
                and len(targets) == len(target_constants), "planned BConv dimensions differ")
        self.load_mod()
        for src, inv, norm in zip(source, inverse, normalized):
            self.load(0, src); self.load(1, inv); self.store(0, norm)
        for constants, target in zip(target_constants, targets):
            require(len(constants) == len(source), "planned BConv matrix differs")
            for norm, constant in zip(normalized, constants):
                self.load(0, norm); self.load(1, constant)
            self.store(2, target)

    def twiddle(self, direction: str, basis: int, phase: str, stage: int = -1):
        leaf = f"stage_{stage:02d}" if phase == "stage" else phase
        path = f"constants/twiddle/{direction}/basis_{basis:02d}/{leaf}.u32.bin"
        return self.whole(path, f"{direction}_twiddle_ctx_{basis}")

    def transform(self, components, contexts, inverse: bool) -> None:
        self.load_mod()
        for component in components:
            require(len(component) == len(contexts), "planned transform dimensions differ")
            for basis, context in enumerate(contexts):
                self.load(0, component[basis])
                if not inverse:
                    self.load(3, self.twiddle("ntt", context, "pre_twist"))
                for stage in range(12):
                    self.load(3, self.twiddle("intt" if inverse else "ntt",
                                              context, "stage", stage))
                if inverse:
                    self.load(3, self.twiddle("intt", context, "post_untwist_scale"))
                self.store(0, component[basis])


def build_plan(catalog: dict[str, tuple[int, int]]) -> list[dict[str, object]]:
    plan = Planner(catalog)
    base = plan.polys("images/input_base_q.u32.bin", 0, NUM_Q, "input.base_q")
    switching = plan.polys("images/input_t2_q.u32.bin", 0, NUM_Q, "input.switching_q")
    workspace = "am/runtime/keyswitch_scratch.u32.bin"
    modup = plan.polys(workspace, 0, TOTAL_QP, "modup.current_qp")
    accum = [plan.polys(workspace, TOTAL_QP, TOTAL_QP, "keyswitch.accum0.qp"),
             plan.polys(workspace, 2 * TOTAL_QP, TOTAL_QP, "keyswitch.accum1.qp")]
    normalized = plan.polys(workspace, 3 * TOTAL_QP, max(DIGIT_SIZE, NUM_P),
                            "keyswitch.bconv_normalized")
    correction = plan.polys(workspace, 3 * TOTAL_QP + max(DIGIT_SIZE, NUM_P), NUM_Q,
                            "keyswitch.moddown_correction_q")
    output = plan.polys("am/output_q.u32.bin", 0, 2 * NUM_Q, "output.ciphertext_q")
    contexts = list(range(TOTAL_QP))

    for digit in range(DNUM):
        q_offset = digit * DIGIT_SIZE
        sources = switching[q_offset:q_offset + DIGIT_SIZE]
        inverses = [plan.poly(SUPPORT_PATHS[0], digit * DIGIT_SIZE + source,
                             f"modup.d{digit}.qhat_inv{source}")
                    for source in range(DIGIT_SIZE)]
        for source, target in zip(sources, modup[q_offset:q_offset + DIGIT_SIZE]):
            plan.load(0, source); plan.store(0, target)
        constants, targets = [], []
        for target in range(TOTAL_QP):
            if q_offset <= target < q_offset + DIGIT_SIZE:
                continue
            constants.append([plan.poly(SUPPORT_PATHS[1],
                (digit * TOTAL_QP + target) * DIGIT_SIZE + source,
                f"modup.d{digit}.target{target}.source{source}")
                for source in range(DIGIT_SIZE)])
            targets.append(modup[target])
        plan.bconv(sources, inverses, constants, normalized[:DIGIT_SIZE], targets)
        plan.transform([modup], contexts, False)
        for component in range(2):
            for basis in range(TOTAL_QP):
                key_index = (digit * 2 + component) * TOTAL_QP + basis
                key = plan.poly("images/rlk_ntt_qp.u32.bin", key_index,
                                f"rlk.d{digit}.c{component}.basis{basis}")
                plan.load(0, modup[basis]); plan.load(1, key)
                if digit:
                    plan.load(2, accum[component][basis])
                plan.store(2, accum[component][basis])

    plan.transform(accum, contexts, True)
    p_inverse = plan.polys(SUPPORT_PATHS[2], 0, NUM_P, "moddown.p_qhat_inverse")
    p_target = [plan.polys(SUPPORT_PATHS[3], target * NUM_P, NUM_P,
                           f"moddown.p_qhat_mod_q.target{target}") for target in range(NUM_Q)]
    p_inverse_q = plan.polys(SUPPORT_PATHS[4], 0, NUM_Q, "moddown.p_inverse_mod_q")
    for component in range(2):
        plan.bconv(accum[component][NUM_Q:], p_inverse, p_target,
                   normalized[:NUM_P], correction)
        plan.load_mod()
        for basis in range(NUM_Q):
            plan.load(0, accum[component][basis]); plan.load(1, correction[basis])
            plan.load(2, p_inverse_q[basis])
            plan.store(0, accum[component][basis] if component == 0 else output[NUM_Q + basis])
    plan.load_mod()
    for basis in range(NUM_Q):
        plan.load(0, accum[0][basis]); plan.load(1, base[basis]); plan.store(2, output[basis])
    require(len(plan.records) == EXPECTED_DMA, "AM KeySwitch planner did not produce 716 DMA rows")
    return plan.records


def prepare(outputs: Path, encodings: Path) -> dict[str, object]:
    key, auto = outputs / "keyswitch", outputs / "auto"
    validate_params(key, "keyswitch")
    validate_params(auto, "auto")
    key_hw, auto_hw = validate_hardware(key), validate_hardware(auto)
    validate_math_and_layout(key, key_hw)
    validate_mod_context(key_hw)
    for path, blob in key_hw["blobs"].items():
        if path == "constants/mod_ctx.u32.bin" or path.startswith("constants/twiddle/"):
            require(auto_hw["blobs"].get(path) == blob,
                    f"auto support batch differs from KeySwitch constants: {path}")
    for path, expected_lines in zip(SUPPORT_PATHS, SUPPORT_LINES):
        require(auto_hw["catalog"].get(path, (None, None))[1] == expected_lines,
                f"auto support artifact has wrong geometry: {path}")

    program, words, relocations = validate_program(key, encodings)
    auto_program = parse_program(auto / "auto.asm", "KEYSWITCH BODY")
    require(auto_program == program, "Auto embedded KeySwitch body differs from standalone program")
    auto_plan, auto_relocations = rows(auto / "test_data/dma_plan.csv"), rows(auto / "dma_relocation_manifest.csv")
    require(len(auto_plan) == len(auto_relocations) and len(auto_plan) > EXPECTED_DMA,
            "auto: resolved plan/relocation counts differ")
    for index, (span, relocation) in enumerate(zip(auto_plan, auto_relocations)):
        require(span["status"] == "RESOLVED"
                and span["instruction_index"] == relocation["instruction_index"]
                and span["dma_index"] == relocation["dma_index"]
                and span["direction"] == relocation["direction"]
                and span["object_slot"] == relocation["obj_id"],
                f"auto: unresolved or mismatched DMA row {index}")
        artifact = span["artifact"]
        require(artifact in auto_hw["catalog"], f"auto: DMA artifact absent from line map: {artifact}")
        base, available = auto_hw["catalog"][artifact]
        offset, count = int(span["line_offset"]), int(span["line_count"])
        require(count > 0 and base <= offset and offset + count <= base + available,
                f"auto: DMA row {index} exceeds its artifact")
    for index, (witness, relocation) in enumerate(
            zip(auto_plan[-EXPECTED_DMA:], relocations)):
        auto_relocation = auto_relocations[-EXPECTED_DMA + index]
        require(witness["direction"] == relocation["direction"]
                and witness["object_slot"] == relocation["obj_id"]
                and auto_relocation["normalized_asm"] == relocation["normalized_asm"]
                and auto_relocation["word_hex"] == relocation["word_hex"],
                f"KeySwitch DMA {index}: Auto resolved-plan witness differs")

    window = bytearray(key_hw["image"])
    combined_rows = [dict(row) for row in key_hw["line_rows"]]
    catalog = dict(key_hw["catalog"])
    cursor = len(window) // LINE_BYTES
    support = {}
    for path in SUPPORT_PATHS:
        raw = auto_hw["blobs"][path]
        count = len(raw) // LINE_BYTES
        catalog[path] = (cursor, count)
        combined_rows.append({"path": path, "role": "same-batch Auto KeySwitch support constant",
                              "shape": "", "address_byte": "AM_RELOCATED",
                              "line_offset": str(cursor), "line_count": str(count),
                              "payload_words": str(len(raw) // 4), "payload_bytes": str(len(raw)),
                              "padded_words": str(len(raw) // 4), "padded_bytes": str(len(raw))})
        support[path] = raw
        window.extend(raw)
        cursor += count

    scratch_polys = 3 * TOTAL_QP + max(DIGIT_SIZE, NUM_P) + NUM_Q
    scratch_lines = scratch_polys * POLY_LINES
    scratch_path = "am/runtime/keyswitch_scratch.u32.bin"
    scratch = struct.pack("<I", 0xA55A5AA5) * (scratch_lines * WORDS_PER_LINE)
    catalog[scratch_path] = (cursor, scratch_lines)
    combined_rows.append({"path": scratch_path, "role": "AM-owned KeySwitch scratch",
                          "shape": f"{scratch_polys}x{N}", "address_byte": "AM_RELOCATED",
                          "line_offset": str(cursor), "line_count": str(scratch_lines),
                          "payload_words": str(scratch_lines * WORDS_PER_LINE),
                          "payload_bytes": str(len(scratch)), "padded_words": str(scratch_lines * WORDS_PER_LINE),
                          "padded_bytes": str(len(scratch))})
    window.extend(scratch); cursor += scratch_lines
    output_lines = 2 * NUM_Q * POLY_LINES
    output_path = "am/output_q.u32.bin"
    output_poison = struct.pack("<I", 0xDEADBEEF) * (output_lines * WORDS_PER_LINE)
    catalog[output_path] = (cursor, output_lines)
    combined_rows.append({"path": output_path, "role": "AM-owned poisoned KeySwitch output",
                          "shape": f"2x{NUM_Q}x{N}", "address_byte": "AM_RELOCATED",
                          "line_offset": str(cursor), "line_count": str(output_lines),
                          "payload_words": str(output_lines * WORDS_PER_LINE),
                          "payload_bytes": str(len(output_poison)),
                          "padded_words": str(output_lines * WORDS_PER_LINE),
                          "padded_bytes": str(len(output_poison))})
    window.extend(output_poison); cursor += output_lines
    window_lines = cursor
    guard = b"".join(struct.pack("<I", (0x4B530000 ^ index * 0x45D9F3B) & 0xFFFFFFFF)
                     for index in range(GUARD_LINES * WORDS_PER_LINE))
    window.extend(guard)

    planned = build_plan(catalog)
    resolved = []
    for index, (span, relocation) in enumerate(zip(planned, relocations)):
        require(span["direction"] == relocation["direction"]
                and int(span["object_slot"]) == int(relocation["obj_id"]),
                f"KeySwitch DMA {index}: AM plan differs from encoded relocation")
        resolved.append({"instruction_index": relocation["instruction_index"],
                         "dma_index": relocation["dma_index"], **span})
    return {"key": key, "auto": auto, "program": program,
            "words": [map_word(word) for word in words], "source": map_c((key / "keyswitch.c").read_text()),
            "resolved": resolved, "line_rows": combined_rows, "support": support,
            "window": bytes(window), "window_lines": window_lines,
            "guard_offset": window_lines, "scratch_offset": catalog[scratch_path][0],
            "output_offset": catalog[output_path][0],
            "golden": key_hw["blobs"]["images/expected_q.u32.bin"]}


def publish(destination: Path, prepared: dict[str, object], commit: str, encodings: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".keyswitch-import-", dir=destination.parent))
    try:
        (staging / "window.u32.bin").write_bytes(prepared["window"])
        (staging / "golden.u32.bin").write_bytes(prepared["golden"])
        (staging / "keyswitch.c").write_text(prepared["source"], encoding="utf-8")
        shutil.copyfile(prepared["key"] / "keyswitch.h", staging / "keyswitch.h")
        (staging / "keyswitch.inst32").write_text(
            "".join(f"{word:032b}\n" for word in prepared["words"]), encoding="ascii")
        shutil.copyfile(prepared["key"] / "keyswitch.cmd26", staging / "keyswitch.cmd26")
        fields = ["instruction_index", "dma_index", "direction", "object_slot",
                  "logical_object", "artifact", "line_offset", "line_count", "status"]
        with (staging / "resolved_dma.tsv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
            writer.writeheader(); writer.writerows(prepared["resolved"])
        with (staging / "line_map.tsv").open("w", encoding="utf-8", newline="") as stream:
            fields = list(prepared["line_rows"][0])
            writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
            writer.writeheader(); writer.writerows(prepared["line_rows"])
        header = ["#ifndef HPU_KEYSWITCH_DELIVERY_H", "#define HPU_KEYSWITCH_DELIVERY_H",
                  '#include "keyswitch.h"', f"#define HPU_KEYSWITCH_DMA_COUNT {EXPECTED_DMA}U",
                  f"#define HPU_KEYSWITCH_WINDOW_LINES {prepared['window_lines']}U",
                  f"#define HPU_KEYSWITCH_GUARD_OFFSET {prepared['guard_offset']}U",
                  f"#define HPU_KEYSWITCH_GUARD_LINES {GUARD_LINES}U",
                  f"#define HPU_KEYSWITCH_TOTAL_LINES {prepared['window_lines'] + GUARD_LINES}U",
                  f"#define HPU_KEYSWITCH_SCRATCH_OFFSET {prepared['scratch_offset']}U",
                  f"#define HPU_KEYSWITCH_OUTPUT_OFFSET {prepared['output_offset']}U",
                  "static const hpu_dma_span_t keyswitch_spans[HPU_KEYSWITCH_DMA_COUNT] = {"]
        header += [f"    {{{row['line_offset']}U, {row['line_count']}U}}, /* {row['logical_object']} */"
                   for row in prepared["resolved"]]
        header += ["};", "#endif", ""]
        (staging / "keyswitch_delivery.h").write_text("\n".join(header), encoding="utf-8")
        (staging / "producer_commit.txt").write_text(commit + "\n", encoding="ascii")
        shutil.copyfile(encodings, staging / "encoder_words.tsv")
        provenance = staging / "upstream"
        provenance.mkdir()
        for name in ("keyswitch.asm", "keyswitch.c", "keyswitch.h", "keyswitch.inst32",
                     "keyswitch.cmd26", "dma_relocation_manifest.csv"):
            shutil.copyfile(prepared["key"] / name, provenance / name)
        for name in ("params.json", "artifact_manifest.csv", "hardware/abi.json",
                     "hardware/hardware_manifest.csv", "hardware/hpu_mem_config.json",
                     "hardware/line_map.csv", "hardware/mod_ctx_map.csv", "hardware/twiddle_map.csv"):
            target = provenance / name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(prepared["key"] / "test_data" / name, target)
        support_root = provenance / "auto-support"
        support_root.mkdir()
        for path, raw in prepared["support"].items():
            target = support_root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
        shutil.copyfile(prepared["auto"] / "test_data/dma_plan.csv", support_root / "dma_plan.csv")
        (staging / "DELIVERY_SUMMARY.md").write_text(
            "# KeySwitch AM import\n\n"
            f"- producer commit: `{commit}`\n- instructions: {EXPECTED_INSTRUCTIONS}\n"
            f"- resolved DMA rows: {EXPECTED_DMA}\n- HPU window lines: {prepared['window_lines']}\n"
            f"- guard lines: {GUARD_LINES}\n- status: semantic import complete; fixture/case integration pending\n",
            encoding="utf-8")
        if destination.exists():
            marker = destination / "DELIVERY_SUMMARY.md"
            require(destination.is_dir() and not destination.is_symlink() and marker.is_file(),
                    "refusing to replace an unowned KeySwitch import directory")
            shutil.rmtree(destination)
        staging.rename(destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="same-batch producer outputs root")
    parser.add_argument("--destination", required=True, type=Path, help="generated keyswitch-data directory")
    parser.add_argument("--producer-commit", required=True)
    parser.add_argument("--encodings", required=True, type=Path)
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit) is not None,
            "producer commit must be a full 40-character Git commit")
    source, destination = args.source.resolve(), args.destination.resolve()
    require(source.is_dir() and args.encodings.is_file(), "producer outputs/encoder table is missing")
    require(source != destination and source not in destination.parents
            and destination not in source.parents, "source and destination must not overlap")
    prepared = prepare(source, args.encodings)
    publish(destination, prepared, args.producer_commit, args.encodings)
    print(f"KeySwitch Q4/P3/D2: {EXPECTED_INSTRUCTIONS} instructions, {EXPECTED_DMA} resolved DMA, "
          f"{prepared['window_lines']} HPU lines imported")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, KeyError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"KeySwitch import failed: {error}") from error
