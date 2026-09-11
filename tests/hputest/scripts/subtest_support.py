#!/usr/bin/env python3
"""03 独立 subtest 的目录及 ELF 静态检查；不把编译成功当成 IT PASS。"""

import csv
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess


CATALOG_FIELDS = (
    "parent_case_id", "subcase", "subtest_id", "programs", "description", "source",
)
PARENT_FIELDS = ("group", "qualifier", "case_id", "source")
PARENTS = tuple(f"HPU_IT_DIR_INS_C0_{number:03d}" for number in range(1, 10))
CHAPTER = "03_compute_instructions"
ELF_ENTRY = 0x80000000


def _tsv(path, fields):
    try:
        with path.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream, delimiter="\t", strict=True)
            if reader.fieldnames != list(fields):
                raise ValueError(f"invalid TSV header: {path}")
            rows = list(reader)
    except (OSError, UnicodeError, csv.Error) as error:
        raise ValueError(f"cannot read TSV {path}: {error}") from error
    for row in rows:
        if None in row or any(value is None or not value.strip() for value in row.values()):
            raise ValueError(f"missing or extra TSV field: {path}")
    return rows


def _source_path(source, parent):
    """只接收章节中的真实用例路径，不允许目录逃逸或目录名冒充用例。"""
    relative = PurePosixPath(source)
    if (relative.is_absolute() or "\\" in source or ".." in relative.parts or
            "." in source.split("/") or "//" in source or
            len(relative.parts) != 4 or relative.parts[:2] != ("src", CHAPTER) or
            relative.name != parent + ".c"):
        raise ValueError(f"invalid or escaping source for {parent}: {source}")
    return relative


def _canonical_number(value, field):
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]*", value):
        raise ValueError(f"invalid {field}: {value!r}")
    return int(value)


def _check_row(row):
    parent = row.get("parent_case_id", "")
    if parent not in PARENTS:
        raise ValueError(f"unknown parent_case_id: {parent}")
    subcase = _canonical_number(row.get("subcase"), "subcase")
    programs = _canonical_number(row.get("programs"), "programs")
    expected_programs = 2 if parent.endswith("_008") else 1
    if programs != expected_programs:
        raise ValueError(f"wrong programs for {parent}: {programs}, expected {expected_programs}")
    prefix = f"{parent}__s{subcase:02d}_"
    identifier = row.get("subtest_id", "")
    if not identifier.startswith(prefix) or not re.fullmatch(
            r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*", identifier[len(prefix):]):
        raise ValueError(f"invalid subtest_id for {parent} subcase={subcase}: {identifier}")
    if not row.get("description", "").strip():
        raise ValueError(f"missing description for {identifier}")
    _source_path(row.get("source", ""), parent)
    return subcase


def load_catalog(test_root: Path) -> list[dict]:
    """核对 37 个独立 ELF / 9 个父用例 / 40 段程序，保持 TSV 字段为字符串。"""
    test_root = Path(test_root).resolve()
    rows = _tsv(test_root / "subtests" / "cases.tsv", CATALOG_FIELDS)
    parent_rows = _tsv(test_root / "cases.tsv", PARENT_FIELDS)
    parents = {}
    for row in parent_rows:
        parent = row["case_id"]
        if parent in parents:
            raise ValueError(f"duplicate parent case in cases.tsv: {parent}")
        parents[parent] = row

    found = {}
    identifiers = set()
    for row in rows:
        index = _check_row(row)
        parent = row["parent_case_id"]
        if row["subtest_id"] in identifiers:
            raise ValueError(f"duplicate subtest_id: {row['subtest_id']}")
        identifiers.add(row["subtest_id"])
        if parent not in parents or parents[parent]["qualifier"] != "software-self-check":
            raise ValueError(f"parent is not a software-self-check case: {parent}")
        if parents[parent]["source"] != row["source"]:
            raise ValueError(f"parent/source mismatch: {parent}")
        indices = found.setdefault(parent, set())
        if index in indices:
            raise ValueError(f"duplicate subcase {index} for {parent}")
        indices.add(index)

    if set(found) != set(PARENTS):
        raise ValueError("catalog must contain all 9 instruction parents")
    for parent, indices in found.items():
        source = _source_path(parents[parent]["source"], parent)
        path = test_root
        for component in source.parts:
            path = path / component
            if path.is_symlink():
                raise ValueError(f"symlink source is not allowed: {path}")
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise ValueError(f"cannot read parent source {path}: {error}") from error
        # 注释中的示意调用不能替代 main 中的实际数量。
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)
        counts = re.findall(r"\bprogress_begin\s*\(\s*__FILE__\s*,\s*([1-9][0-9]*)[uU]?\s*\)", text)
        if len(counts) != 1:
            raise ValueError(f"expected one literal progress_begin count: {path}")
        expected = set(range(int(counts[0])))
        if indices != expected:
            raise ValueError(f"missing or out-of-range subcase for {parent}: "
                             f"found={sorted(indices)}, source count={counts[0]}")
    if len(rows) != 37 or sum(int(row["programs"]) for row in rows) != 40:
        raise ValueError("catalog must contain 37 subtests and 40 programs")
    return rows


def _elf_image(path):
    """仅解析需要的 ELF64/PT_LOAD 信息，不依赖 strings 的模糊结果。"""
    path = Path(path)
    if path.is_symlink():
        raise ValueError(f"symlink ELF is not allowed: {path}")
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read ELF {path}: {error}") from error
    if (len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01"):
        raise ValueError(f"not a little-endian ELF64 image: {path}")
    header = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
    kind, machine, version, entry, phoff = header[:5]
    ehsize, phentsize, phnum = header[7:10]
    if kind != 2 or machine != 243 or version != 1 or entry != ELF_ENTRY:
        raise ValueError(f"ELF must be RISC-V executable with entry=0x{ELF_ENTRY:x}: {path}")
    if ehsize != 64 or phentsize != 56 or phnum == 0 or phoff < 64:
        raise ValueError(f"invalid ELF program headers: {path}")
    if phoff + phentsize * phnum > len(data):
        raise ValueError(f"truncated ELF program headers: {path}")
    loads = []
    for index in range(phnum):
        record = struct.unpack_from("<IIQQQQQQ", data, phoff + index * phentsize)
        ptype, flags, offset, vaddr, _paddr, filesz, memsz, _align = record
        if ptype != 1:
            continue
        if filesz > memsz or offset + filesz > len(data):
            raise ValueError(f"invalid or truncated PT_LOAD: {path}")
        loads.append((vaddr, filesz, offset, flags))
    if not any(address <= entry < address + size and flags & 1
               for address, size, _offset, flags in loads):
        raise ValueError(f"ELF entry is not file-backed executable memory: {path}")
    return data, loads


def _symbols(elf, cross_compile):
    try:
        output = subprocess.run(
            [cross_compile + "nm", "-S", "--defined-only", "--format=posix", str(elf)],
            check=True, capture_output=True, text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError, UnicodeError) as error:
        raise ValueError(f"cannot read ELF symbols for {elf}: {error}") from error
    symbols = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) not in (3, 4):
            continue
        name, kind, value = fields[:3]
        if not re.fullmatch(r"[0-9a-fA-F]+", value):
            continue
        size = fields[3] if len(fields) == 4 else "0"
        if not re.fullmatch(r"[0-9a-fA-F]+", size):
            continue
        if name in symbols:
            raise ValueError(f"ambiguous ELF symbol: {name}")
        symbols[name] = (int(value, 16), int(size, 16), kind)
    return symbols


def _symbol_bytes(data, loads, symbols, name, max_bytes):
    if name not in symbols:
        raise ValueError(f"missing ELF symbol: {name}")
    address, size, _kind = symbols[name]
    mappings = [(base, length, offset) for base, length, offset, _flags in loads
                if base <= address < base + length]
    if len(mappings) != 1:
        raise ValueError(f"symbol is not uniquely file-backed: {name}")
    base, length, offset = mappings[0]
    available = base + length - address
    if size and size > available:
        raise ValueError(f"symbol extends outside PT_LOAD: {name}")
    start = offset + address - base
    if size:
        return data[start:start + size]
    if max_bytes <= 0:
        raise ValueError("max_bytes must be positive for a size-less symbol")
    sample = data[start:start + min(available, max_bytes)]
    terminator = sample.find(b"\0")
    if terminator < 0:
        raise ValueError(f"size-less string symbol is not NUL terminated: {name}")
    return sample[:terminator + 1]


def read_symbol_bytes(elf: Path, symbol: str, cross_compile: str,
                      max_bytes: int = 64) -> bytes:
    """读真实加载字节：有 size 时读全长；无 size 时限长读到 NUL（包含 NUL）。"""
    data, loads = _elf_image(elf)
    return _symbol_bytes(data, loads, _symbols(elf, cross_compile), symbol, max_bytes)


def verify_elf(elf: Path, row: dict, cross_compile: str) -> None:
    """确保 ELF 确实只选择目录指定子项；不验证硬件执行结果。"""
    subcase = _check_row(row)
    data, loads = _elf_image(elf)
    symbols = _symbols(elf, cross_compile)
    for name in ("main", "progress_begin", "subcase_selected"):
        if name not in symbols:
            raise ValueError(f"missing ELF symbol: {name}")
        address, size, kind = symbols[name]
        if kind not in ("T", "t") or size == 0 or not any(
                base <= address and address + size <= base + length and flags & 1
                for base, length, _offset, flags in loads):
            raise ValueError(f"missing real executable function: {name}")
    for name in ("RNS_A", "RNS_B"):
        vector = _symbol_bytes(data, loads, symbols, name, 64)
        if len(vector) != 4096 * 4:
            raise ValueError(f"{name} is not an embedded 4096-u32 input")
    argument = _symbol_bytes(data, loads, symbols, "__am_mainargs", 64)
    expected = f"subcase={subcase}".encode("ascii") + b"\0"
    if argument != expected:
        raise ValueError(f"wrong __am_mainargs: expected {expected!r}, found {argument!r}")
    source = ("tests/hputest/" + row["source"]).encode("utf-8")
    # Makefile.case 的 AM_HOME 前缀映射可能产生 nexus-am/ 前缀；两者都精确比较。
    identities = {source, b"nexus-am/" + source}
    if not any(identity in data[offset:offset + length].split(b"\0")
               for _base, length, offset, _flags in loads for identity in identities):
        raise ValueError(f"wrong parent source identity in loadable ELF: {row['source']}")
