#!/usr/bin/env python3
"""将普通/静默 workload 与 03 subtest 合成一个安全、可追溯的下载包。"""

import argparse
import csv
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CHAPTERS = (
    "00_bringup", "01_configuration", "02_data_paths",
    "03_compute_instructions", "04_composite_instruction_sequences",
    "05_cpu_hpu_structural_connectivity", "06_performance", "07_full_application",
)
INSTRUCTION_IDS = {f"HPU_IT_DIR_INS_C0_{number:03d}" for number in range(1, 10)}
WORKLOAD_FIELDS = (
    "chapter", "case_id", "qualifier", "publish_status", "source",
    "elf", "bin", "disassembly", "notes",
)
SUBTEST_FIELDS = (
    "parent_case_id", "subcase", "subtest_id", "programs", "description",
    "source", "mainargs", "elf", "bin", "disassembly", "build_status",
    "log_level", "log_mode",
)
INDEX_FIELDS = (
    "chapter", "kind", "test_id", "parent_case_id", "subcase", "variant",
    "log_mode", "uart_results", "qualifier", "publish_status", "source",
    "mainargs", "elf", "bin", "disassembly", "description", "notes",
)
FILE_FIELDS = (("elf", ".elf"), ("bin", ".bin"), ("disassembly", ".txt"))
OUTPUT_MARKER = ".hpu-unified-output"
PACKAGE_MARKER = ".hpu-unified-package"
FILE_LIST = ".package-files.tsv"
OUTPUT_IDENTITY = f"hpu-unified-output-v1\nsource={ROOT}\n"
PACKAGE_IDENTITY = "hpu-unified-package-v1\n"


def tree_files(root):
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f"expected a plain directory: {root}")
    files = []
    for directory, names, leaves in os.walk(root, followlinks=False):
        for name in names + leaves:
            item = Path(directory) / name
            if item.is_symlink():
                raise ValueError(f"symlink is not allowed: {item}")
            mode = item.stat().st_mode
            if not stat.S_ISDIR(mode) and not stat.S_ISREG(mode):
                raise ValueError(f"special file is not allowed: {item}")
        files.extend(Path(directory, name).relative_to(root).as_posix() for name in leaves)
    return sorted(files)


def resolve_release(value):
    requested = Path(value).absolute()
    if requested.is_symlink() or requested.name != "release":
        raise ValueError(f"input must be a plain directory named release: {requested}")
    release = requested.resolve(strict=True)
    tree_files(release)
    return release


def regular_file(root, value, label):
    relative = PurePosixPath(value)
    if (not value or relative.is_absolute() or ".." in relative.parts or
            "\\" in value or any(ord(character) < 32 for character in value)):
        raise ValueError(f"{label}: invalid package-relative path: {value!r}")
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"{label}: symlink is not allowed: {value}")
    if not current.is_file() or current.stat().st_size == 0:
        raise ValueError(f"{label}: missing or empty file: {current}")
    if not current.resolve().is_relative_to(root):
        raise ValueError(f"{label}: path escapes package: {value}")
    return current, relative


def read_tsv(path, fields):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t", strict=True)
        if reader.fieldnames != list(fields):
            raise ValueError(f"invalid columns: {path}")
        rows = list(reader)
    if any(None in row or any(value is None for value in row.values()) for row in rows):
        raise ValueError(f"malformed row: {path}")
    return rows


def read_manifest(release):
    path, _ = regular_file(release, "MANIFEST.txt", "manifest")
    metadata = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if not separator or not key or key in metadata:
            raise ValueError(f"invalid or duplicate manifest field: {path}")
        metadata[key] = value
    return metadata


def require_metadata(metadata, expected, label):
    mismatches = {key: (metadata.get(key), value) for key, value in expected.items()
                  if metadata.get(key) != value}
    if mismatches:
        raise ValueError(f"{label} has wrong build mode: {mismatches}")


def compare_metadata(manifests):
    for field in ("revision", "arch", "inline_asm_commit"):
        values = {metadata.get(field) for metadata in manifests.values()}
        if None in values or len(values) != 1:
            raise ValueError(f"input packages disagree on {field}: {values}")
    hpu_seal_values = {manifests[name].get("hpu_seal_commit")
                       for name in ("workloads", "silent-workloads")}
    if None in hpu_seal_values or len(hpu_seal_values) != 1:
        raise ValueError(f"workload packages disagree on hpu_seal_commit: {hpu_seal_values}")


def compare_trees(left, right, label):
    left_files = tree_files(left)
    right_files = tree_files(right)
    if left_files != right_files:
        raise ValueError(f"{label} file lists differ")
    for relative in left_files:
        if (left / relative).read_bytes() != (right / relative).read_bytes():
            raise ValueError(f"{label} differs: {relative}")


def workload_rows(release, mode):
    rows = read_tsv(release / "INDEX.tsv", WORKLOAD_FIELDS)
    if len(rows) != 62:
        raise ValueError(f"{mode} workloads must describe 62 canonical cases")
    seen = set()
    for row in rows:
        identity = row["case_id"]
        if identity in seen or row["chapter"] not in CHAPTERS:
            raise ValueError(f"duplicate/invalid workload row: {identity}")
        seen.add(identity)
        paths = [row[field] for field, _extension in FILE_FIELDS]
        blocked = row["qualifier"] == "blocked-not-issued"
        if blocked != (not any(paths)) or (not blocked and not all(paths)):
            raise ValueError(f"inconsistent published files for workload: {identity}")
        for field, extension in FILE_FIELDS:
            if row[field]:
                _source, relative = regular_file(release, row[field], identity)
                if relative.suffix != extension:
                    raise ValueError(f"wrong {field} extension for workload: {identity}")
    blocked = sum(row["qualifier"] == "blocked-not-issued" for row in rows)
    published = sum(bool(row["elf"]) for row in rows)
    if (published, blocked) != (49, 13):
        raise ValueError(f"{mode} workloads require 49 published and 13 blocked cases")
    instruction = {row["case_id"] for row in rows
                   if row["chapter"] == "03_compute_instructions" and row["elf"]}
    if instruction != INSTRUCTION_IDS:
        raise ValueError(f"{mode} workloads must contain the nine 03 parent cases")
    return rows


def compare_workloads(normal, silent):
    keys = ("chapter", "case_id", "qualifier", "publish_status", "source",
            "elf", "bin", "disassembly")
    left = [{key: row[key] for key in keys} for row in normal]
    right = [{key: row[key] for key in keys} for row in silent]
    if left != right:
        raise ValueError("normal and silent workload indexes do not describe the same cases")


def subtest_rows(release, mode):
    rows = read_tsv(release / "INDEX.tsv", SUBTEST_FIELDS)
    if len(rows) != 37 or len({row["subtest_id"] for row in rows}) != 37:
        raise ValueError(f"{mode} subtests must contain 37 unique rows")
    parents = {row["parent_case_id"] for row in rows}
    if parents != INSTRUCTION_IDS or sum(int(row["programs"]) for row in rows) != 40:
        raise ValueError(f"{mode} subtests must cover nine parents and 40 programs")
    for row in rows:
        if row["build_status"] != "BUILD_READY_NOT_IT_PASS":
            raise ValueError(f"unexpected subtest build status: {row['subtest_id']}")
        expected_mode = ("0", "silent") if mode == "silent" else ("1", "minimal")
        if (row["log_level"], row["log_mode"]) != expected_mode:
            raise ValueError(f"{mode} subtest has inconsistent row mode: {row['subtest_id']}")
        for field, extension in FILE_FIELDS:
            _source, relative = regular_file(release, row[field], row["subtest_id"])
            if relative.suffix != extension:
                raise ValueError(f"wrong {field} extension for subtest: {row['subtest_id']}")
    return rows


def compare_subtests(normal, silent):
    keys = ("parent_case_id", "subcase", "subtest_id", "programs", "description",
            "source", "mainargs", "elf", "bin", "disassembly", "build_status")
    left = [{key: row[key] for key in keys} for row in normal]
    right = [{key: row[key] for key in keys} for row in silent]
    if left != right:
        raise ValueError("normal and silent subtest indexes do not describe the same cases")


def variant_path(relative, silent):
    return relative.with_name(relative.stem + ("_silent" if silent else "") + relative.suffix)


def add_files(release, row, silent, copies, destinations):
    output = {}
    for field, extension in FILE_FIELDS:
        source, relative = regular_file(release, row[field], row.get("test_id", "artifact"))
        if relative.suffix != extension:
            raise ValueError(f"wrong extension for {relative}")
        destination = variant_path(relative, silent)
        if destination in destinations:
            raise ValueError(f"duplicate unified destination: {destination}")
        destinations.add(destination)
        copies.append((source, destination))
        output[field] = destination.as_posix()
    return output


def merge_tools(destination, sources):
    destination.mkdir()
    for source in sources:
        if not source.exists():
            continue
        for relative in tree_files(source):
            incoming = source / relative
            target = destination / relative
            if target.exists():
                if target.read_bytes() != incoming.read_bytes():
                    raise ValueError(f"tool differs between packages: {relative}")
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(incoming, target)


def prepare_output(value):
    output = Path(value).absolute()
    if output.is_symlink():
        raise ValueError("output root cannot be a symlink")
    output = output.resolve()
    if output in (ROOT, ROOT.parent, ROOT.parents[1], Path.home(), Path("/")):
        raise ValueError("output root must be a dedicated unified build directory")
    if output.exists():
        tree_files(output)
        marker = output / OUTPUT_MARKER
        if any(output.iterdir()) and (not marker.is_file() or
                                     marker.read_text(encoding="utf-8") != OUTPUT_IDENTITY):
            raise ValueError(f"refusing an unowned nonempty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / OUTPUT_MARKER).write_text(OUTPUT_IDENTITY, encoding="utf-8")
    return output


def validate_existing_release(release):
    tree_files(release)
    children = list(release.iterdir())
    if len(children) != 1 or children[0].name != "hputest" or not children[0].is_dir():
        raise ValueError("refusing to replace malformed unified release")
    package = children[0]
    marker = package / PACKAGE_MARKER
    file_list = package / FILE_LIST
    if (not marker.is_file() or marker.read_text(encoding="utf-8") != PACKAGE_IDENTITY or
            not file_list.is_file()):
        raise ValueError("refusing to replace unmarked unified release")
    if tree_files(package) != file_list.read_text(encoding="utf-8").splitlines():
        raise ValueError("unified release has untracked files; preserve them before repackaging")


def publish(staging, destination):
    backup = None
    if destination.exists() or destination.is_symlink():
        if destination.is_symlink() or not destination.is_dir():
            raise ValueError("unified release destination is not a plain directory")
        validate_existing_release(destination)
        backup = Path(tempfile.mkdtemp(prefix=".release-previous-", dir=destination.parent))
        backup.rmdir()
        destination.rename(backup)
    try:
        staging.rename(destination)
    except OSError:
        if backup is not None:
            backup.rename(destination)
        raise
    if backup is not None:
        print(f"Previous unified package preserved: {backup}")


def write_index(path, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=INDEX_FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def package(workloads, silent_workloads, subtests, silent_subtests, output_root):
    releases = {
        "workloads": resolve_release(workloads),
        "silent-workloads": resolve_release(silent_workloads),
        "subtests": resolve_release(subtests),
        "silent-subtests": resolve_release(silent_subtests),
    }
    manifests = {name: read_manifest(release) for name, release in releases.items()}
    require_metadata(manifests["workloads"], {
        "selection": "all", "log_level": "1", "log_mode": "minimal",
        "uart_results": "brief", "hpu_dump_results": "0",
    }, "workloads")
    require_metadata(manifests["silent-workloads"], {
        "selection": "all", "log_level": "0", "log_mode": "silent",
        "uart_results": "none", "hpu_dump_results": "0",
    }, "silent workloads")
    require_metadata(manifests["subtests"], {
        "format": "hpu-subtests-v1", "log_level": "1", "log_mode": "minimal",
        "uart_results": "brief", "hpu_dump_results": "0", "subtests": "37",
    }, "subtests")
    require_metadata(manifests["silent-subtests"], {
        "format": "hpu-subtests-v1", "log_level": "0", "log_mode": "silent",
        "uart_results": "none", "hpu_dump_results": "0", "subtests": "37",
    }, "silent subtests")
    compare_metadata(manifests)
    compare_trees(releases["workloads"] / "provenance",
                  releases["silent-workloads"] / "provenance", "workload provenance")
    compare_trees(releases["subtests"] / "provenance",
                  releases["silent-subtests"] / "provenance", "subtest provenance")

    normal_workloads = workload_rows(releases["workloads"], "normal")
    quiet_workloads = workload_rows(releases["silent-workloads"], "silent")
    compare_workloads(normal_workloads, quiet_workloads)
    normal_subtests = subtest_rows(releases["subtests"], "normal")
    quiet_subtests = subtest_rows(releases["silent-subtests"], "silent")
    compare_subtests(normal_subtests, quiet_subtests)

    output_candidate = Path(output_root).absolute().resolve()
    if any(output_candidate == release or output_candidate.is_relative_to(release) or
           release.is_relative_to(output_candidate) for release in releases.values()):
        raise ValueError("output root must not overlap an input release")
    output = prepare_output(output_candidate)
    staging = Path(tempfile.mkdtemp(prefix=".release-staging-", dir=output))
    package_root = staging / "hputest"
    package_root.mkdir()
    indexes = []
    copies = []
    destinations = set()
    try:
        silent_by_id = {row["case_id"]: row for row in quiet_workloads}
        for normal in normal_workloads:
            if normal["chapter"] == "03_compute_instructions":
                continue
            if not normal["elf"]:
                indexes.append({
                    "chapter": normal["chapter"], "kind": "workload",
                    "test_id": normal["case_id"], "parent_case_id": "", "subcase": "",
                    "variant": "", "log_mode": "", "uart_results": "",
                    "qualifier": normal["qualifier"],
                    "publish_status": normal["publish_status"], "source": normal["source"],
                    "mainargs": "all", "elf": "", "bin": "", "disassembly": "",
                    "description": "", "notes": normal["notes"],
                })
                continue
            for release_name, row, silent in (
                    ("workloads", normal, False),
                    ("silent-workloads", silent_by_id[normal["case_id"]], True)):
                entry = {
                    "chapter": row["chapter"], "kind": "workload", "test_id": row["case_id"],
                    "parent_case_id": "", "subcase": "", "variant": "silent" if silent else "normal",
                    "log_mode": "silent" if silent else "minimal",
                    "uart_results": "none" if silent else "brief", "qualifier": row["qualifier"],
                    "publish_status": row["publish_status"], "source": row["source"],
                    "mainargs": "all", "description": "", "notes": row["notes"],
                }
                entry.update(add_files(releases[release_name], row, silent, copies, destinations))
                indexes.append(entry)

        silent_subtests_by_id = {row["subtest_id"]: row for row in quiet_subtests}
        for normal in normal_subtests:
            for release_name, row, silent in (
                    ("subtests", normal, False),
                    ("silent-subtests", silent_subtests_by_id[normal["subtest_id"]], True)):
                entry = {
                    "chapter": "03_compute_instructions", "kind": "subtest",
                    "test_id": row["subtest_id"], "parent_case_id": row["parent_case_id"],
                    "subcase": row["subcase"], "variant": "silent" if silent else "normal",
                    "log_mode": row["log_mode"], "uart_results": "none" if silent else "brief",
                    "qualifier": "software-self-check", "publish_status": row["build_status"],
                    "source": row["source"], "mainargs": row["mainargs"],
                    "description": row["description"],
                    "notes": "独立仿真子项；全部兄弟子项通过才算父用例完整覆盖",
                }
                entry.update(add_files(releases[release_name], row, silent, copies, destinations))
                indexes.append(entry)

        for source, relative in copies:
            destination = package_root / Path(relative.as_posix())
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

        shutil.copytree(releases["workloads"] / "provenance", package_root / "provenance")
        manifests_root = package_root / "provenance" / "build-manifests"
        manifests_root.mkdir()
        for name, release in releases.items():
            shutil.copy2(release / "MANIFEST.txt", manifests_root / f"{name}.txt")
        (package_root / "provenance" / "testplan").mkdir(exist_ok=True)
        shutil.copy2(releases["subtests"] / "cases.tsv",
                     package_root / "provenance" / "testplan" / "SUBTEST_CASES.tsv")
        merge_tools(package_root / "tools", [releases["workloads"] / "tools",
                                               releases["subtests"] / "tools"])

        indexes.sort(key=lambda row: (row["chapter"], row["source"], row["test_id"], row["variant"]))
        write_index(package_root / "INDEX.tsv", indexes)
        for chapter in CHAPTERS:
            chapter_rows = [row for row in indexes if row["chapter"] == chapter]
            directory = package_root / chapter
            directory.mkdir(exist_ok=True)
            write_index(directory / "INDEX.tsv", chapter_rows)

        published = sum(bool(row["elf"]) for row in indexes)
        blocked = sum(row["publish_status"] == "BLOCKED_NOT_PUBLISHED" for row in indexes)
        if (published, blocked, len(indexes)) != (154, 13, 167):
            raise ValueError(f"unexpected unified counts: published={published} blocked={blocked} rows={len(indexes)}")
        metadata = manifests["workloads"]
        (package_root / "MANIFEST.txt").write_text(
            "format=hpu-unified-package-v1\n"
            f"revision={metadata['revision']}\narch={metadata['arch']}\n"
            f"inline_asm_commit={metadata['inline_asm_commit']}\n"
            f"hpu_seal_commit={metadata['hpu_seal_commit']}\n"
            "variants=normal,silent\nnormal_log_mode=minimal\nsilent_log_mode=silent\n"
            "parent_instruction_cases_replaced=9\nsubtests=37\n"
            "published_test_identities=77\npublished_variant_sets=154\n"
            "blocked_index_only=13\nqualification=BUILD_READY_NOT_IT_PASS\n",
            encoding="utf-8")
        (package_root / "README.md").write_text(
            "# HPU 统一测试包\n\n"
            "本包把原 workload 与 03 独立 subtest 合并到同一棵 00 至 07 章节目录。"
            "03 不发布原先一个 ELF 串行执行多轮的九个父用例，只发布37个独立子项。\n\n"
            "每个已发布测试有两种文件：普通 minimal UART 版保留原名，真正无 UART 的版本在扩展名前加 "
            "`_silent`，例如 `foo.elf` 与 `foo_silent.elf`。BIN 和反汇编 TXT 使用同一规则。\n\n"
            "普通版仍执行完整自检并输出必要的开始、最终结果和失败摘要；silent 版执行相同指令、同步、"
            "golden 与 guard 检查，但不打印，必须依靠仿真终止状态判定。"
            "构建成功不代表已经在 IT/VCS 上运行通过。\n\n"
            "`INDEX.tsv` 标明 kind、父用例、subcase、variant 和真实路径。"
            "13个尚未接入的测试只保留索引与原因，不发布占位二进制。"
            "并行运行03子项可使用 `tools/run-subtests.py`。\n",
            encoding="utf-8")
        (package_root / PACKAGE_MARKER).write_text(PACKAGE_IDENTITY, encoding="ascii")
        (package_root / FILE_LIST).write_text("", encoding="utf-8")
        (package_root / FILE_LIST).write_text("\n".join(tree_files(package_root)) + "\n",
                                               encoding="utf-8")
        publish(staging, output / "release")
    except Exception:
        if staging.exists():
            shutil.rmtree(staging)
        raise
    print(f"Unified HPU package: {output / 'release' / 'hputest'}; "
          "77 tests x 2 variants, 13 blocked index-only")
    return output / "release"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workloads", type=Path, required=True)
    parser.add_argument("--silent-workloads", type=Path, required=True)
    parser.add_argument("--subtests", type=Path, required=True)
    parser.add_argument("--silent-subtests", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    try:
        package(args.workloads, args.silent_workloads, args.subtests,
                args.silent_subtests, args.output_root)
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(2, f"package-unified: {error}\n")


if __name__ == "__main__":
    main()
