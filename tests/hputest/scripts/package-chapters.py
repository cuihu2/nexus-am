#!/usr/bin/env python3
"""将已经校验的 HPU 构建产物按章节发布；不把编译成功当作 IT 通过。"""

import argparse
import csv
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import tempfile


GROUPS = {"core", "transform", "fhe"}
QUALIFIERS = {
    "software-self-check", "blocked-not-issued", "waveform-hold",
    "termination-probe-pass", "termination-probe-fail",
}
CHAPTERS = {
    "00_bringup", "01_configuration", "02_data_paths",
    "03_compute_instructions", "04_composite_instruction_sequences",
    "05_cpu_hpu_structural_connectivity", "06_performance", "07_full_application",
}
DIAGNOSTIC_IDS = (
    {f"HPU_IT_DIR_INS_C0_{index:03d}" for index in range(1, 10)} |
    {f"HPU_IT_DIR_CMB_{index:03d}" for index in range(1, 4)}
)
EXTENSIONS = (".elf", ".bin", ".txt")
MANIFESTS = ("MANIFEST.txt", "CASE_MANIFEST.tsv", "NOT_QUALIFIED.tsv")
MARKER = ".hpu-chapter-package"
FILE_LIST = ".package-files.tsv"
INDEX_FIELDS = (
    "chapter", "case_id", "qualifier", "publish_status", "source",
    "elf", "bin", "disassembly", "notes",
)


def regular_file(root, relative):
    """拒绝软链接、缺文件和空文件，不能通过路径别名带入其它目录的数据。"""
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"symlink is not allowed: {current}")
    if not current.is_file() or current.stat().st_size == 0:
        raise ValueError(f"missing or empty file: {current}")
    return current


def read_tsv(path, fields):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != list(fields):
            raise ValueError(f"invalid columns: {path}")
        rows = list(reader)
    if any(None in row or any(value is None for value in row.values()) for row in rows):
        raise ValueError(f"malformed row: {path}")
    return rows


def source_path(value, case_id):
    source = PurePosixPath(value)
    if (source.is_absolute() or source.as_posix() != value or
            len(source.parts) != 4 or source.parts[0] != "src" or
            source.parts[1] not in CHAPTERS or
            not re.fullmatch(r"[A-Za-z0-9_-]+", source.parts[2]) or
            not re.fullmatch(r"[A-Za-z0-9_]+", case_id) or
            source.name != case_id + ".c"):
        raise ValueError(f"invalid/escaping source path: {value}")
    return PurePosixPath(*source.parts[1:])


def tree_files(root):
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f"expected plain directory: {root}")
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


def write_index(path, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=INDEX_FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def package(artifact, require_all=False, require_diagnostic=False):
    requested = Path(artifact).absolute()
    if requested.is_symlink() or requested.name != "artifact":
        raise ValueError("input must be a plain directory named artifact")
    artifact = requested.resolve(strict=True)
    if not artifact.is_dir():
        raise ValueError("artifact is not a directory")
    # 不提供任意输出路径选项：只允许本次构建的同级 release 目录。
    release = artifact.parent / "release"
    marker_value = f"package-chapters-v1\nsource={artifact}\n"
    for name in MANIFESTS:
        regular_file(artifact, PurePosixPath(name))
    rows = read_tsv(artifact / "CASE_MANIFEST.tsv", ("group", "qualifier", "case_id", "source"))
    blocked_rows = read_tsv(artifact / "NOT_QUALIFIED.tsv", ("case_id", "source", "reason"))
    metadata = {}
    for line in (artifact / "MANIFEST.txt").read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if not separator or key in metadata:
            raise ValueError("invalid or duplicate MANIFEST metadata")
        metadata[key] = value
    if not rows or metadata.get("case_count") != str(len(rows)):
        raise ValueError("MANIFEST case_count does not match CASE_MANIFEST")
    if require_all and metadata.get("selection") != "all":
        raise ValueError("--all requires a full selection=all build")
    if (metadata.get("uart_results"), metadata.get("hpu_dump_results")) not in {
            ("brief", "0"), ("full", "1")}:
        raise ValueError("missing/inconsistent UART build mode metadata")
    if require_all and metadata["uart_results"] != "brief":
        raise ValueError("default --all package requires brief UART output")
    if require_diagnostic != (metadata.get("selection") == "diagnostic"):
        raise ValueError("--diagnostic and selection=diagnostic must be used together")
    if require_diagnostic and metadata["uart_results"] != "full":
        raise ValueError("diagnostic package requires full UART output")
    if metadata["uart_results"] == "full" and not require_diagnostic:
        raise ValueError("full UART chapter packages require --diagnostic")

    blocked = {}
    for row in blocked_rows:
        if row["case_id"] in blocked or not row["reason"].strip():
            raise ValueError("duplicate blocked case or missing not-qualified reason")
        blocked[row["case_id"]] = row
    seen_ids, seen_paths, declared_blocked = set(), set(), set()
    indexes, copies = [], []
    for row in rows:
        case_id, qualifier, group = row["case_id"], row["qualifier"], row["group"]
        relative = source_path(row["source"], case_id)
        if group not in GROUPS or qualifier not in QUALIFIERS:
            raise ValueError(f"invalid group/qualifier: {case_id}")
        if case_id in seen_ids or relative in seen_paths:
            raise ValueError(f"duplicate case ID or destination: {case_id}")
        seen_ids.add(case_id)
        seen_paths.add(relative)
        entry = dict.fromkeys(INDEX_FIELDS, "")
        entry.update(chapter=relative.parts[0], case_id=case_id,
                     qualifier=qualifier, source=row["source"])
        if qualifier == "blocked-not-issued":
            if case_id not in blocked or blocked[case_id]["source"] != row["source"]:
                raise ValueError(f"missing/mismatched blocked reason: {case_id}")
            declared_blocked.add(case_id)
            entry.update(publish_status="BLOCKED_NOT_PUBLISHED", notes=blocked[case_id]["reason"])
        else:
            entry["publish_status"] = "BUILD_READY_NOT_IT_PASS"
            entry["notes"] = "需要在匹配的 IT/simv 上运行；编译通过不等于功能通过"
            if require_diagnostic:
                entry["notes"] += "；全量 UART 诊断版本，打印 HPU/golden 每项数据，运行明显更慢"
            if qualifier == "waveform-hold":
                entry["notes"] = "故意无限等待看波形；必须设置仿真 cycle-limit，不等待 PASS"
            elif qualifier == "termination-probe-fail":
                entry["publish_status"] = "EXPECTED_FAIL_PROBE_RETURN_1"
                entry["notes"] = "故意 return 1 的终止探针；不得计入功能失败或自动 PASS 列表"
            elif qualifier == "termination-probe-pass":
                entry["notes"] = "故意 return 0 的终止探针；只验证结束通道，不验证 HPU 功能"
            for extension, field in zip(EXTENSIONS, ("elf", "bin", "disassembly")):
                destination = relative.with_suffix(extension)
                source = regular_file(artifact, PurePosixPath(group) / destination)
                copies.append((source, destination))
                entry[field] = destination.as_posix()
        indexes.append(entry)
    if declared_blocked != set(blocked):
        raise ValueError("NOT_QUALIFIED contains undeclared or non-blocked cases")
    if metadata.get("not_qualified_count") != str(len(declared_blocked)):
        raise ValueError("MANIFEST not_qualified_count does not match blocked cases")
    if require_diagnostic:
        if seen_ids != DIAGNOSTIC_IDS or any(
                row["qualifier"] != "software-self-check" or
                row["chapter"] != (
                    "03_compute_instructions" if "_INS_" in row["case_id"]
                    else "04_composite_instruction_sequences")
                for row in indexes):
            raise ValueError("diagnostic package must contain exactly the twelve ready 03/04 cases")
    if require_all:
        instruction_ids = {
            row["case_id"] for row in indexes
            if row["chapter"] == "03_compute_instructions"
        }
        required = {f"HPU_IT_DIR_INS_C0_{index:03d}" for index in range(1, 10)}
        if not required.issubset(instruction_ids):
            raise ValueError("full package must include all nine 03 instruction cases, including 005/006")

    provenance = artifact / "provenance"
    if not tree_files(provenance):
        raise ValueError("missing provenance files")
    # 只有本脚本曾生成且文件清单完整的 release 才允许被替换。
    if release.exists() or release.is_symlink():
        existing = tree_files(release)
        if (not (release / MARKER).is_file() or
                (release / MARKER).read_text(encoding="utf-8") != marker_value or
                not (release / FILE_LIST).is_file()):
            raise ValueError("refusing to replace an unmarked release directory")
        declared = (release / FILE_LIST).read_text(encoding="utf-8").splitlines()
        if existing != declared:
            raise ValueError("release has untracked files; preserve them before repackaging")

    staging = Path(tempfile.mkdtemp(prefix=".release-staging-", dir=artifact.parent))
    backup = None
    try:
        for name in MANIFESTS:
            shutil.copy2(artifact / name, staging / name)
        shutil.copytree(provenance, staging / "provenance")
        if (artifact / "tools").exists():
            tree_files(artifact / "tools")
            shutil.copytree(artifact / "tools", staging / "tools")
        for source, relative in copies:
            destination = staging / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        indexes.sort(key=lambda row: (row["chapter"], row["source"]))
        write_index(staging / "INDEX.tsv", indexes)
        chapters = sorted({row["chapter"] for row in indexes})
        summary = []
        for chapter in chapters:
            chapter_rows = [row for row in indexes if row["chapter"] == chapter]
            (staging / chapter).mkdir(exist_ok=True)
            write_index(staging / chapter / "INDEX.tsv", chapter_rows)
            count = sum(bool(row["elf"]) for row in chapter_rows)
            summary.append(f"| {chapter} | {len(chapter_rows)} | {count} | {len(chapter_rows) - count} |")
        uart_note = (
            "## 全量 UART 正确性诊断版（不是加速包）\n\n"
            "本包只包含 03 的九个用例以及 04 的 BConv、整体 NTT、整体 INTT，共 12 个 ELF。"
            "构建参数为 `HPU_DUMP_RESULTS=1`；每个已执行的结果比较都会保留完整的 "
            "4096 项 HPU 实际结果及软件 golden，多 RNS 时逐分量打印，不以抽样代替正确性检查。\n\n"
            "全量串口输出会明显增加仿真 cycle 和现实耗时；只对需要定位的用例使用本包，"
            "并为 UART 输出单独预留仿真周期。它不是提速版本，也不改变比较标准。"
            "默认下载并使用 `nexus-am-hpu-workloads` 常规摘要包；本包另名为 "
            "`nexus-am-hpu-uart-results`，请勿混用两包的 ELF/BIN。\n\n"
            if require_diagnostic else
            "## 默认 UART 摘要版\n\n"
            "构建参数为 `HPU_DUMP_RESULTS=0`，打印阶段、结果统计和错误项；完整正确性比较仍然执行。"
            "03/04 如需保存每项 HPU/golden 数据，请单独下载 `nexus-am-hpu-uart-results`。"
            "全量诊断版明显更慢，不作为日常回归默认包。\n\n"
        )
        readme = (
            "# HPU 按章节测试包\n\n" + uart_note +
            f"本包 mainargs={metadata.get('mainargs', 'all')}；subcase=N只覆盖对应子项。\n\n" +
            "阶段耗时/结果导出见 [运行诊断](provenance/testplan/docs/RUNTIME_UART_DIAGNOSTICS.md)，"
            "导出工具为 tools/parse-uart-results.py。\n\n" +
            "此包按测试源码章节组织，03 的 PNTT/PINTT 不再分散到其它下载包。\n\n"
            "**BUILD_READY_NOT_IT_PASS 仅表示构建和产物校验通过，不表示 IT/VCS 已通过。**\n"
            "请使用与本包编码兼容的 simv，按 INDEX.tsv 的 qualifier 和 notes 选择用例。\n\n"
            "完整范围/剩余缺口见 [v2覆盖说明](provenance/testplan/docs/V2_COVERAGE.md)。"
            "BConv/整体NTT/INTT目前仅有固定基础组合，软件可运行不等于测试点全覆盖。\n\n"
            "- blocked-not-issued 只保留索引及 NOT_QUALIFIED.tsv 原因，不发布占位 ELF/BIN/TXT。\n"
            "- waveform-hold 故意不返回，需要 cycle-limit 并检查波形。\n"
            "- termination-probe-fail 故意 return 1；它是预期失败探针，不是功能用例失败。\n"
            "- termination-probe-pass 只验证 return 0 的终止通道。\n"
            "- ELF/BIN/TXT 均在对应章节子目录；provenance 和原始构建清单只保存一份。\n"
            "- 原 CASE_MANIFEST.tsv 保留构建 group；下载路径以 INDEX.tsv 为准。\n\n"
            "| 章节 | 清单用例 | 发布产物组 | 未就绪（无二进制） |\n"
            "| --- | ---: | ---: | ---: |\n" + "\n".join(summary) + "\n"
        )
        (staging / "README.md").write_text(readme, encoding="utf-8")
        (staging / MARKER).write_text(marker_value, encoding="utf-8")
        (staging / FILE_LIST).write_text("", encoding="utf-8")
        (staging / FILE_LIST).write_text("\n".join(tree_files(staging)) + "\n", encoding="utf-8")
        if release.exists():
            backup = Path(tempfile.mkdtemp(prefix=".release-previous-", dir=artifact.parent))
            backup.rmdir()  # 仅移除刚创建的空目录，再把旧发布包移到这个确定路径。
            release.rename(backup)
        try:
            staging.rename(release)
        except OSError:
            if backup is not None:
                backup.rename(release)
            raise
    except Exception:
        # staging 是本调用独占新建的目录，不清理旧包、源 artifact 或其它工作目录。
        if staging.exists():
            shutil.rmtree(staging)
        raise
    if backup is not None:
        print(f"Previous generated package preserved: {backup}")
    print(f"Chapter package: {release}; cases={len(indexes)} "
          f"published={len(copies) // 3} blocked={len(declared_blocked)}")
    return release


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--all", action="store_true", help="要求完整摘要构建且 03 包含全部九条指令")
    mode.add_argument("--diagnostic", action="store_true", help="要求 03/04 十二项全量 UART 诊断构建")
    args = parser.parse_args()
    try:
        package(args.artifact, require_all=args.all,
                require_diagnostic=args.diagnostic)
    except (OSError, ValueError) as error:
        parser.exit(2, f"package-chapters: {error}\n")


if __name__ == "__main__":
    main()
