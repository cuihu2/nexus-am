#!/usr/bin/env python3
"""读取固定版本的活动HpuCmdMerge做独立回归；不修改RTL，不冒充整机VCS通过。"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PATH = "rtl/cpu/SCPU_RTL/RTL/latest/HpuCmdMerge.sv"
REF = "0cfd995302af926aa22767e145aade9fc9d21ca2"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rtl-repository", required=True, type=Path)
    parser.add_argument("--ref", default=REF)
    parser.add_argument("--iverilog", default="iverilog")
    parser.add_argument("--vvp", default="vvp")
    parser.add_argument("--ivl-base", type=Path)
    args = parser.parse_args()
    for tool in (args.iverilog, args.vvp):
        if shutil.which(tool) is None:
            parser.error(f"required RTL simulator unavailable: {tool}")

    def read(path):
        return subprocess.check_output(["git", "-C", str(args.rtl_repository),
                                        "show", f"{args.ref}:{path}"], text=True)
    rtl = read(PATH)
    filelist = read("ver/it/core/Difftest/FullSys.f")
    if "RTL/latest/HpuCmdMerge.sv" not in filelist:
        parser.error("selected RTL is not in the active FullSys.f")
    cycles = re.search(r"CMD_TIMEOUT_CYCLES\s*=\s*(\d+)", rtl)
    if not cycles or int(cycles[1]) != 500000 or "io_cmd_timeout_fault_code" not in rtl:
        parser.error("selected version does not implement the testcase watchdog contract")

    # 小型回归产物保留在磁盘build目录供查证；不往/tmp写RTL/仿真输出。
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="watchdog-rtl-", dir=build))
    source = output / "HpuCmdMerge.sv"
    source.write_text(rtl, encoding="utf-8")
    binary = output / "watchdog.vvp"
    command = [args.iverilog, "-g2012", "-s", "watchdog_merge_tb"]
    if args.ivl_base:
        command += ["-B", str(args.ivl_base)]
    subprocess.run(command + ["-o", str(binary), str(source),
                             str(ROOT / "scripts/watchdog_merge_tb.sv")], check=True)
    log = subprocess.check_output([args.vvp, str(binary)], text=True)
    (output / "result.txt").write_text(log, encoding="utf-8")
    print(log, end="")
    print(f"RTL evidence: {output} (ref={args.ref}, active path={PATH})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
