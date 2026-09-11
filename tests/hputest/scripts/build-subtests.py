#!/usr/bin/env python3
"""将03的独立选择项分别链接为ELF；不复制C源码，不改完整用例流程。"""

import argparse
import csv
import importlib.util
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

from subtest_support import load_catalog, verify_elf


ROOT = Path(__file__).resolve().parents[1]
MARKER = ".hpu-subtests-build"
INDEX_FIELDS = ("parent_case_id", "subcase", "subtest_id", "programs", "description",
                "source", "mainargs", "elf", "bin", "disassembly", "build_status")


def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


def plain_tree(path):
    if path.is_symlink() or not path.is_dir():
        raise ValueError(f"expected a plain directory: {path}")
    for folder, directories, files in os.walk(path):
        for name in directories + files:
            entry = Path(folder) / name
            if entry.is_symlink() or not (entry.is_dir() or entry.is_file()):
                raise ValueError(f"unexpected link/special file: {entry}")


def prepare_output(output):
    """只使用专有构建目录；旧交付整体保留，不递归清除用户目录。"""
    if output.is_symlink():
        raise ValueError("output root cannot be a symlink")
    output = output.resolve()
    if output in (ROOT, ROOT.parent, ROOT.parents[1], Path.home(), Path("/")):
        raise ValueError("output root must be a dedicated subtest build directory")
    identity = f"hpu-subtests-v1\nsource={ROOT}\n"
    if output.exists():
        plain_tree(output)
        marker = output / MARKER
        if any(output.iterdir()) and (not marker.is_file() or marker.read_text() != identity):
            raise ValueError(f"refusing an unowned nonempty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / MARKER).write_text(identity, encoding="utf-8")
    return output


def check_delivery(generated, am_home):
    submodule = ROOT / "third_party/inline-asm"
    commit = subprocess.check_output(["git", "-C", str(submodule), "rev-parse", "HEAD"], text=True).strip()
    gitlink = subprocess.check_output(
        ["git", "-C", str(am_home), "ls-files", "-s", "--",
         "tests/hputest/third_party/inline-asm"], text=True).split()
    if len(gitlink) < 2 or gitlink[:2] != ["160000", commit]:
        raise ValueError("inline-asm checkout does not match the AM gitlink")
    run(["git", "-C", str(submodule), "diff", "--quiet"])
    run(["git", "-C", str(submodule), "diff", "--cached", "--quiet"])
    header = (generated / "include/hpu/inline_asm_mm_delivery.h").read_text()
    if f'#define HPU_INLINE_ASM_SOURCE_COMMIT "{commit}"' not in header:
        raise ValueError("generated header is stale; run make verify-inline-asm")
    for relative in ("inline-asm/mm/PRODUCER_COMMIT",
                     "instruction-data/provenance/producer_commit.txt"):
        if (generated / relative).read_text().strip() != commit:
            raise ValueError(f"stale producer provenance: {relative}")
    for relative in ("inline-asm/mm", "instruction-data"):
        plain_tree(generated / relative)
    # 每批只验证一次共享交付；各ELF仍逐个核对实际嵌入选择号与机器码。
    spec = importlib.util.spec_from_file_location("subtest_mm_import", ROOT / "scripts/import-inline-asm-mm.py")
    mm_import = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mm_import)
    mm_import.validate_program(generated / "inline-asm/mm")
    mm_import.validate_data(generated / "inline-asm/mm")
    mm_import.validate_mapped_delivery(generated / "inline-asm/mm")
    return commit


def check_commands(disassembly, allowed):
    """只扫实际代码中的HPU指令，不把rodata当指令或全局禁止DASICS。"""
    active, count = False, 0
    pattern = re.compile(r"(?:main|op_\w+|psync|pmodld|pfree|dload|dstore_\w+|issue_transform)(?:\..*)?$")
    for line in disassembly.splitlines():
        label = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if label:
            active = pattern.fullmatch(label[1]) is not None
        instruction = re.match(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})(?:\s|$)", line)
        if not instruction:
            continue
        word = int(instruction[1], 16)
        opcode = word & 127
        if active and opcode == 0x0b:
            raise ValueError("subtest contains a legacy HPU custom0 word")
        if opcode in (0x5b, 0x2b):
            if word not in allowed:
                raise ValueError(f"instruction absent from producer encoding table: 0x{word:08x}")
            count += 1
    if not count:
        raise ValueError("subtest has no HPU instruction")


def publish(staging, destination):
    if destination.exists():
        plain_tree(destination)
        if not (destination / MARKER).is_file():
            raise ValueError(f"refusing to replace unmarked deliverable: {destination}")
        backup = Path(tempfile.mkdtemp(prefix=f".{destination.name}-previous-", dir=destination.parent))
        backup.rmdir()  # 本调用刚创建的空目录；旧交付移入后仍可恢复。
        destination.rename(backup)
        print(f"Previous subtest deliverable preserved: {backup}", flush=True)
    staging.rename(destination)


def build(output, generated, arch, cross, jobs, dump):
    rows = load_catalog(ROOT)
    am_home = ROOT.parents[1]
    generated = generated.resolve(strict=True)
    commit = check_delivery(generated, am_home)
    output = prepare_output(output)
    for tool in ("make", cross + "gcc", cross + "strip", cross + "objcopy", cross + "objdump", cross + "nm"):
        if shutil.which(tool) is None:
            raise ValueError(f"missing build tool: {tool}")
    with (generated / "inline-asm/mm/encoder_words.tsv").open() as source:
        allowed = {int(row["word_hex"], 0) for row in csv.DictReader(source, delimiter="\t")}
    staging = Path(tempfile.mkdtemp(prefix=".artifact-staging-", dir=output))
    # 每个simv可并行运行，但链接必须串行：AM库共享mainargs.S，不能并发改写它。
    (staging / "03_compute_instructions").mkdir()
    (staging / "logs").mkdir()
    index = []
    environment = dict(os.environ, AM_HOME=str(am_home))
    try:
        for row in rows:
            relative = Path("03_compute_instructions") / row["subtest_id"]
            binary = staging / relative
            objects = output / "obj" / f"uart-{dump}" / row["parent_case_id"]
            print(f"[hputest][subtest] {row['subtest_id']} mainargs=subcase={row['subcase']}", flush=True)
            command = ["make", "-C", str(ROOT), "-f", "Makefile.case", f"-j{jobs}",
                       f"ARCH={arch}", f"CROSS_COMPILE={cross}", "LINUX_GNU_TOOLCHAIN=1",
                       f"CASE_SOURCE={ROOT / row['source']}", f"CASE_ID={row['parent_case_id']}",
                       f"HPU_DST_DIR={objects}/", f"BINARY={binary}",
                       f"HPU_GENERATED_ROOT={generated}", f"HPU_DUMP_RESULTS={dump}",
                       f"mainargs=subcase={row['subcase']}"]
            with (staging / "logs" / (row["subtest_id"] + ".log")).open("w") as log:
                run(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
            elf = binary.with_suffix(".elf")
            run([cross + "strip", "--strip-debug", str(elf)])
            run([cross + "objcopy", "-O", "binary", str(elf), str(binary.with_suffix(".bin"))])
            disassembly = subprocess.check_output([cross + "objdump", "-d", elf.name], cwd=elf.parent, text=True)
            binary.with_suffix(".txt").write_text(disassembly, encoding="utf-8")
            verify_elf(elf, row, cross)
            check_commands(disassembly, allowed)
            index.append(dict(row, mainargs=f"subcase={row['subcase']}",
                              elf=relative.with_suffix(".elf").as_posix(),
                              bin=relative.with_suffix(".bin").as_posix(),
                              disassembly=relative.with_suffix(".txt").as_posix(),
                              build_status="BUILD_READY_NOT_IT_PASS"))
        with (staging / "INDEX.tsv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=INDEX_FIELDS, delimiter="\t")
            writer.writeheader()
            writer.writerows(index)
        revision = subprocess.check_output(["git", "-C", str(am_home), "rev-parse", "HEAD"], text=True).strip()
        (staging / "MANIFEST.txt").write_text(
            f"format=hpu-subtests-v1\nrevision={revision}\ninline_asm_commit={commit}\n"
            f"arch={arch}\nparent_cases=9\nsubtests={len(rows)}\nprograms=40\n"
            f"hpu_dump_results={dump}\nuart_results={'full' if dump else 'brief'}\n"
            "execution=one-independent-simv-per-elf\nqualification=BUILD_READY_NOT_IT_PASS\n", encoding="utf-8")
        shutil.copyfile(ROOT / "subtests/cases.tsv", staging / "cases.tsv")
        readme = (ROOT / "subtests/README.md").read_text(encoding="utf-8")
        note = f"本包实际构建模式：HPU_DUMP_RESULTS={dump}（{'全量输出，更慢' if dump else '常规摘要'}）。\n\n"
        (staging / "README.md").write_text(readme.replace("\n\n", "\n\n" + note, 1), encoding="utf-8")
        for source, destination in (("inline-asm/mm", "inline-asm-mm"), ("instruction-data", "instruction-data")):
            shutil.copytree(generated / source, staging / "provenance" / destination)
        (staging / "tools").mkdir()
        for name in ("run-subtests.py", "parse-uart-results.py"):
            shutil.copyfile(ROOT / "scripts" / name, staging / "tools" / name)
        (staging / MARKER).write_text("hpu-subtests-v1\n", encoding="ascii")
        publish(staging, output / "artifact")
        release = Path(tempfile.mkdtemp(prefix=".release-staging-", dir=output))
        shutil.copytree(output / "artifact", release, dirs_exist_ok=True)
        publish(release, output / "release")
    except Exception:
        # 保留失败日志与已生成的部分产物，但绝不把它们发布成完整release。
        print(f"Incomplete subtest build preserved for diagnosis: {staging}", flush=True)
        raise
    print(f"[hputest] subtests PASS: 37 independent ELF/BIN/TXT sets; release={output / 'release'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--generated-root", type=Path, required=True)
    parser.add_argument("--arch", default="riscv64-xs", choices=("riscv64-xs",))
    parser.add_argument("--cross-compile", default="riscv64-linux-gnu-")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--dump-results", type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        build(args.output_root, args.generated_root, args.arch, args.cross_compile, args.jobs, args.dump_results)
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Subtest build failed: {error}\n")


if __name__ == "__main__":
    main()
