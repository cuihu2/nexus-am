#!/usr/bin/env python3
"""Validate all testcase descriptors against the authoritative HPU IT xlsx.

The parser intentionally uses only Python's standard library so the check can
run on an IT build host without openpyxl or LibreOffice.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import re
import sys
import zipfile
from pathlib import Path, PurePosixPath
from xml.etree import ElementTree as ET


EXPECTED_XLSX_SHA256 = (
    "a483d9b10c8626ced01d9e3cad55ce044e1884980b6c1cc9390d35737129f83d"
)
MAIN_NS = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
DOC_REL_NS = (
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
)
PKG_REL_NS = "http://schemas.openxmlformats.org/package/2006/relationships"

FEATURE_DIR = {
    "HPU配置相关测试": "01_configuration",
    "HPU通路测试": "02_data_paths",
    "HPU计算指令测试": "03_compute_instructions",
    "HPU组合指令序列测试": "04_composite_instruction_sequences",
    "核与HPU结构连接测试": "05_cpu_hpu_structural_connectivity",
    "HPU性能测试": "06_performance",
    "HPU完整应用测试": "07_full_application",
}

# The frozen workbook still labels these rows as unsupported.  Their IDs,
# testpoint linkage, priority and category remain authoritative; only the
# implementation-status title/type changed after executable operator packages
# were delivered.
DESCRIPTOR_IMPLEMENTATION_OVERRIDES = {
    "HPU_IT_DIR_CMB_011": (
        "HPU_IT_DIR_CMB_011",
        "IT-CMB-011",
        "Poseidon Encode完整内联HPU序列与NTT域golden",
        "定向算法库算子",
        2,
    ),
    "HPU_IT_DIR_CMB_013": (
        "HPU_IT_DIR_CMB_013",
        "IT-CMB-013",
        "Poseidon Rescale完整内联HPU序列与降层golden",
        "定向算法库算子",
        2,
    ),
}


class ValidationError(RuntimeError):
    pass


def qname(namespace: str, local: str) -> str:
    return f"{{{namespace}}}{local}"


def column_index(reference: str) -> int:
    letters = "".join(char for char in reference if char.isalpha())
    result = 0
    for char in letters:
        result = result * 26 + ord(char.upper()) - ord("A") + 1
    if result == 0:
        raise ValidationError(f"invalid cell reference: {reference}")
    return result - 1


def xlsx_sheets(path: Path) -> dict[str, list[list[str | None]]]:
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as exc:
        raise ValidationError(f"cannot open xlsx {path}: {exc}") from exc

    with archive:
        try:
            shared_root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
            workbook = ET.fromstring(archive.read("xl/workbook.xml"))
            rel_root = ET.fromstring(
                archive.read("xl/_rels/workbook.xml.rels")
            )
        except (KeyError, ET.ParseError) as exc:
            raise ValidationError(f"malformed xlsx {path}: {exc}") from exc

        shared_strings = [
            "".join(node.text or "" for node in item.iter(qname(MAIN_NS, "t")))
            for item in shared_root.findall(qname(MAIN_NS, "si"))
        ]
        relationships = {
            relation.attrib["Id"]: relation.attrib["Target"]
            for relation in rel_root.findall(qname(PKG_REL_NS, "Relationship"))
        }

        result: dict[str, list[list[str | None]]] = {}
        sheet_list = workbook.find(qname(MAIN_NS, "sheets"))
        if sheet_list is None:
            raise ValidationError("xlsx workbook has no sheets")
        for sheet in sheet_list:
            name = sheet.attrib["name"]
            relation_id = sheet.attrib[qname(DOC_REL_NS, "id")]
            target = relationships.get(relation_id)
            if target is None:
                raise ValidationError(f"xlsx sheet {name} has no relationship")
            if target.startswith("/"):
                member = target.lstrip("/")
            else:
                member = str(PurePosixPath("xl") / target)
            try:
                root = ET.fromstring(archive.read(member))
            except (KeyError, ET.ParseError) as exc:
                raise ValidationError(f"cannot read xlsx sheet {name}: {exc}") from exc

            rows: list[list[str | None]] = []
            for row in root.iter(qname(MAIN_NS, "row")):
                values: list[str | None] = []
                for cell in row.findall(qname(MAIN_NS, "c")):
                    index = column_index(cell.attrib["r"])
                    while len(values) <= index:
                        values.append(None)
                    cell_type = cell.attrib.get("t")
                    if cell_type == "inlineStr":
                        values[index] = "".join(
                            node.text or ""
                            for node in cell.iter(qname(MAIN_NS, "t"))
                        )
                        continue
                    value = cell.find(qname(MAIN_NS, "v"))
                    if value is None or value.text is None:
                        values[index] = None
                    elif cell_type == "s":
                        try:
                            values[index] = shared_strings[int(value.text)]
                        except (IndexError, ValueError) as exc:
                            raise ValidationError(
                                f"sheet {name}: invalid shared-string index"
                            ) from exc
                    else:
                        values[index] = value.text
                rows.append(values)
            result[name] = rows
        return result


def value_at(row: list[str | None], index: int) -> str:
    if index >= len(row) or row[index] is None:
        return ""
    return str(row[index])


def split_macro_arguments(body: str) -> list[str]:
    arguments: list[str] = []
    start = 0
    depth = 0
    in_string = False
    escaped = False
    for index, char in enumerate(body):
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char in "([{" :
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            arguments.append(body[start:index].strip())
            start = index + 1
    arguments.append(body[start:].strip())
    return arguments


def descriptor(path: Path) -> tuple[str, str, str, str, int]:
    text = path.read_text(encoding="utf-8")
    matches = list(
        re.finditer(r"HPU_DEFINE_TESTCASE\s*\((.*?)\)\s*;", text, re.DOTALL)
    )
    if len(matches) != 1:
        raise ValidationError(
            f"{path}: expected exactly one HPU_DEFINE_TESTCASE, found {len(matches)}"
        )
    arguments = split_macro_arguments(matches[0].group(1))
    if len(arguments) != 8:
        raise ValidationError(
            f"{path}: expected 8 descriptor arguments, found {len(arguments)}"
        )
    strings: list[str] = []
    for token in arguments[:4]:
        try:
            parsed = ast.literal_eval(token)
        except (SyntaxError, ValueError) as exc:
            raise ValidationError(f"{path}: malformed string argument {token}") from exc
        if not isinstance(parsed, str):
            raise ValidationError(f"{path}: descriptor string is not a string: {token}")
        strings.append(parsed)
    try:
        priority = int(arguments[4], 0)
    except ValueError as exc:
        raise ValidationError(
            f"{path}: priority is not an integer: {arguments[4]}"
        ) from exc
    return strings[0], strings[1], strings[2], strings[3], priority


def validate(repo_root: Path, xlsx: Path, allow_other_hash: bool) -> None:
    digest = hashlib.sha256(xlsx.read_bytes()).hexdigest()
    if digest != EXPECTED_XLSX_SHA256 and not allow_other_hash:
        raise ValidationError(
            "xlsx SHA-256 mismatch: "
            f"expected={EXPECTED_XLSX_SHA256} observed={digest}"
        )

    sheets = xlsx_sheets(xlsx)
    if "测试用例" not in sheets or "测试点分解" not in sheets:
        raise ValidationError("xlsx must contain 测试用例 and 测试点分解 sheets")

    table_cases: dict[str, tuple[str, str, str, str, int]] = {}
    feature_by_case: dict[str, str] = {}
    for row in sheets["测试用例"]:
        case_id = value_at(row, 0)
        if not case_id.startswith("HPU_IT_"):
            continue
        if len(row) < 13 or any(not value_at(row, index) for index in range(13)):
            raise ValidationError(f"xlsx testcase {case_id} has an empty A-M field")
        if case_id in table_cases:
            raise ValidationError(f"duplicate xlsx testcase ID: {case_id}")
        priority_text = value_at(row, 5)
        if not re.fullmatch(r"P[0-3]", priority_text):
            raise ValidationError(
                f"xlsx testcase {case_id} has invalid priority {priority_text}"
            )
        table_cases[case_id] = (
            case_id,
            value_at(row, 1),
            value_at(row, 3),
            value_at(row, 4),
            int(priority_text[1]),
        )
        feature_by_case[case_id] = value_at(row, 2)

    table_points = set()
    for row in sheets["测试点分解"]:
        match = re.match(r"(IT-[A-Z0-9-]+)", value_at(row, 4))
        if match:
            table_points.add(match.group(1))

    source_paths = sorted(
        path
        for directory in FEATURE_DIR.values()
        for path in (repo_root / directory).glob("*/*.c")
        if path.name.startswith("HPU_IT_")
    )
    if len(source_paths) != 49:
        raise ValidationError(f"expected 49 testcase sources, found {len(source_paths)}")

    source_cases: dict[str, tuple[str, str, str, str, int]] = {}
    path_by_case: dict[str, Path] = {}
    for path in source_paths:
        parsed = descriptor(path)
        case_id = parsed[0]
        if case_id in source_cases:
            raise ValidationError(f"duplicate descriptor ID: {case_id}")
        if path.stem != case_id:
            raise ValidationError(
                f"descriptor/file mismatch: file={path.stem} descriptor={case_id}"
            )
        source_cases[case_id] = parsed
        path_by_case[case_id] = path

    missing = sorted(set(table_cases) - set(source_cases))
    extra = sorted(set(source_cases) - set(table_cases))
    if missing or extra:
        raise ValidationError(
            f"xlsx/source testcase mismatch: missing={missing} extra={extra}"
        )
    if len(table_cases) != 49:
        raise ValidationError(f"expected 49 xlsx testcases, found {len(table_cases)}")

    mismatches = []
    for case_id, expected in table_cases.items():
        observed = source_cases[case_id]
        descriptor_expected = DESCRIPTOR_IMPLEMENTATION_OVERRIDES.get(
            case_id, expected
        )
        if observed != descriptor_expected:
            mismatches.append(
                f"{case_id}: expected={descriptor_expected!r} "
                f"observed={observed!r}"
            )
        feature = feature_by_case[case_id]
        expected_directory = FEATURE_DIR.get(feature)
        actual_directory = path_by_case[case_id].relative_to(repo_root).parts[0]
        if expected_directory is None:
            mismatches.append(f"{case_id}: unknown xlsx feature {feature!r}")
        elif actual_directory != expected_directory:
            mismatches.append(
                f"{case_id}: expected directory {expected_directory}, "
                f"observed {actual_directory}"
            )
    if mismatches:
        raise ValidationError("descriptor mismatches:\n  " + "\n  ".join(mismatches))

    case_points = {record[1] for record in table_cases.values()}
    if case_points != table_points:
        raise ValidationError(
            "testpoint linkage mismatch: "
            f"only-in-cases={sorted(case_points - table_points)} "
            f"only-in-points={sorted(table_points - case_points)}"
        )
    if len(table_points) != 46:
        raise ValidationError(f"expected 46 testpoints, found {len(table_points)}")

    print(
        "PASS: authoritative xlsx matches 49 testcase descriptors, "
        "46 testpoints, and 7 category directories with "
        f"{len(DESCRIPTOR_IMPLEMENTATION_OVERRIDES)} documented "
        f"implementation-status overrides (sha256={digest})"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("xlsx", type=Path, help="path to HPU-IT测试点分解.xlsx")
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="IT-SCPU-TestCases root (default: script parent)",
    )
    parser.add_argument(
        "--allow-other-hash",
        action="store_true",
        help="validate schema/content without enforcing the frozen workbook hash",
    )
    args = parser.parse_args()
    try:
        validate(args.repo_root.resolve(), args.xlsx.resolve(), args.allow_other_hash)
    except (OSError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
