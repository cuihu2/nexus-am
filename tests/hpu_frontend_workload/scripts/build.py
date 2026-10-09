#!/usr/bin/env python3
"""单独构建新API workload，保留旧工作负载/依赖/产物不变。"""
import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile

from import_package import import_package, read_json, require

ROOT = Path(__file__).resolve().parents[1]


def run(arguments, **kwargs):
    subprocess.run([str(arg) for arg in arguments], check=True, **kwargs)


def output(arguments):
    return subprocess.check_output([str(arg) for arg in arguments], text=True).strip()


def prepare_producer(source, build, jobs):
    pin = read_json(ROOT / "producer.json")["commit"]
    require(output(["git", "-C", source, "rev-parse", "HEAD"]) == pin, "new API gitlink/pin mismatch")
    require(not output(["git", "-C", source, "status", "--porcelain", "--untracked-files=no"]),
            "producer must be clean")
    run(["cmake", "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
         "-DHPU_ENABLE_SEAL_INTEGRATION=ON", "-DHPU_ENABLE_SEAL_DIFFERENTIAL_ORACLE=OFF",
         "-DHPU_ENABLE_LEGACY_FIXED_PROFILE_TESTS=OFF", "-DHPU_ENABLE_DEPLOYMENT_APPLICATION_TESTS=OFF"])
    targets = [f"hpu_{scheme}_evaluator_example" for scheme in ("ckks", "bfv", "bgv")]
    run(["cmake", "--build", build, "--target", *targets, "hpu_validate_package", "-j", jobs])
    return pin


def prepare_cases(args, specs, pin):
    validator = args.producer_build / "hpu_validate_package"
    data_root = args.output / "generated"
    deliveries = []
    for spec in specs:
        source = args.packages / spec["case_id"]
        if not source.exists():
            source.parent.mkdir(parents=True, exist_ok=True)
            # 只调用新版现成example；不使用允许instruction-model失败的diagnostic开关。
            example = args.producer_build / f'hpu_{spec["scheme"]}_evaluator_example'
            run([example, "--degree", spec["degree"], "--emit-dir", source])
        destination = data_root / spec["case_id"]
        if destination.exists():
            meta = read_json(destination / "delivery.json")
            require(meta.get("producer_commit") == pin and all(meta.get(k) == v for k, v in spec.items()),
                    "existing imported delivery differs; use a fresh OUTPUT_ROOT")
        else:
            run([validator, source])
            meta = import_package(source, destination, spec, pin)
        deliveries.append((spec, source, destination, meta))
    return deliveries


def verify_elf(path, cross, meta):
    data = path.read_bytes()
    require(data[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", data, 18)[0] == 243,
            "expected little-endian RV64 ELF")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    size, count = struct.unpack_from("<HH", data, 54)
    for index in range(count):
        kind, flags, off, va, pa, filesz, memsz, alignment = struct.unpack_from("<IIQQQQQQ", data, phoff + index * size)
        if kind == 1 and memsz:
            require(pa + memsz <= 0x87000000, "ELF LOAD segment overlaps workload DDR")
    # GCC会把大函数拆成.part.0或.constprop.0；按实际text符号查找，不能假定原名保留。
    prefix = "hpu_program_" + meta["program_stem"]
    symbols = output([cross + "nm", "-S", "--defined-only", path]).splitlines()
    names = [row.split()[-1] for row in symbols if len(row.split()) == 4 and
             row.split()[-2] in ("T", "t") and
             (row.split()[-1] == prefix or row.split()[-1].startswith(prefix + "."))]
    require(bool(names), "missing native program text symbol")
    disassembly = "\n".join(output([cross + "objdump", "-d", "--disassemble=" + name, path]) for name in names)
    words = [int(word, 16) for word in re.findall(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s", disassembly, re.M)]
    return [word for word in words if word & 0x7f in (0x2b, 0x5b)]


def build_case(args, spec, data, meta, mode):
    case_dir = args.output / "artifact" / mode / spec["case_id"]
    case_dir.mkdir(parents=True, exist_ok=True)
    binary = case_dir / spec["case_id"]
    objects = args.output / "objects" / mode / str(args.wait_cycles) / spec["case_id"]
    # AM共享库和mainargs.S不可并行改写，ELF逐个构建；多个ELF在IT外部可并行运行。
    run(["make", "-s", "-C", ROOT, "-f", "Makefile.case", "image",
         f"AM_HOME={args.am_home}", f"ARCH={args.arch}", f"CROSS_COMPILE={args.cross}",
         f'CASE_ID={spec["case_id"]}', f'CASE_SOURCE={ROOT / spec["source"]}',
         f"DATA_DIR={data}", f'PROGRAM_STEM={meta["program_stem"]}',
         f"LOG_LEVEL={int(mode != 'silent')}", f"WAIT_CYCLES={args.wait_cycles}",
         f"CASE_DST_DIR={objects}", f"BINARY={binary}"])
    run([args.cross + "strip", "--strip-debug", str(binary) + ".elf"])
    actual_words = verify_elf(Path(str(binary) + ".elf"), args.cross, meta)
    package = read_json(args.packages / spec["case_id"] / "package.json")
    expected_words = [int(word, 2) for word in
                      (args.packages / spec["case_id"] / package["program_inst32"]).read_text().split()]
    require(actual_words == expected_words, "ELF HPU words differ from native package")
    if mode == "silent":
        run(["python3", args.am_home / "tests/hputest/scripts/verify-silent-elf.py",
             "--elf", str(binary) + ".elf", "--cross-compile", args.cross])
    shutil.copy2(ROOT / spec["source"], case_dir / "case.c")
    for filename in ("layout.h", "delivery.json"):
        shutil.copy2(data / filename, case_dir / filename)
    return {**meta, "mode": mode, "wait_cycle_budget": args.wait_cycles,
            "elf_instruction_words_verified": True,
            "elf": str((case_dir / (spec["case_id"] + ".elf")).relative_to(args.output / "artifact"))}


def publish(args, deliveries, rows):
    artifact = args.output / "artifact"
    for spec, source, data, meta in deliveries:
        target = artifact / "delivery" / spec["case_id"]
        shutil.copytree(source, target / "upstream", dirs_exist_ok=True)
        # native包保留完整来源；目标镜像单独保存，可检查输出毒化和guard。
        shutil.copytree(data, target / "target", dirs_exist_ok=True)
    (artifact / "index.json").write_text(json.dumps({
        "workload": "hpu_frontend_workload", "baseline_modified": False,
        "producer": read_json(ROOT / "producer.json"),
        "nexus_am_commit": output(["git", "-C", args.am_home, "rev-parse", "HEAD"]),
        "nexus_am_worktree_state": "dirty" if output(
            ["git", "-C", args.am_home, "status", "--porcelain", "--untracked-files=no"]) else "clean",
        "status": "BUILD_READY_NOT_IT_PASS", "cases": rows}, indent=2) + "\n")
    shutil.copy2(ROOT / "README.md", artifact / "README.md")
    for filename in ("producer.json", "cases.tsv"):
        shutil.copy2(ROOT / filename, artifact / filename)
    archive = args.output / "hpu-frontend-workload.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
        for path in sorted(artifact.rglob("*")):
            if path.is_file():
                zipped.write(path, path.relative_to(artifact))
    print(f"New frontend workload ready: {archive} ({len(rows)} ELF variants; VCS NOT RUN)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("am-home", "output", "producer", "producer-build", "packages"):
        parser.add_argument("--" + name, required=True, type=lambda value: Path(value).resolve())
    parser.add_argument("--arch", default="riscv64-xs", choices=["riscv64-xs"])
    parser.add_argument("--cross", default="riscv64-linux-gnu-")
    parser.add_argument("--mode", default="both", choices=["both", "minimal", "silent"])
    parser.add_argument("--jobs", default=4, type=int)
    parser.add_argument("--wait-cycles", default=25000000, type=int)
    args = parser.parse_args()
    require(args.jobs > 0 and 0 < args.wait_cycles < 1 << 63, "invalid jobs/cycle budget")
    require(args.output != args.am_home / "tests/hputest/build", "never use the baseline output root")
    args.output.mkdir(parents=True, exist_ok=True)
    with (ROOT / "cases.tsv").open() as stream:
        specs = list(csv.DictReader(stream, delimiter="\t"))
    pin = prepare_producer(args.producer, args.producer_build, args.jobs)
    deliveries = prepare_cases(args, specs, pin)
    modes = ("minimal", "silent") if args.mode == "both" else (args.mode,)
    rows = [build_case(args, spec, data, meta, mode) for mode in modes
            for spec, source, data, meta in deliveries]
    publish(args, deliveries, rows)


if __name__ == "__main__":
    main()
