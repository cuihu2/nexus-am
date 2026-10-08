#!/usr/bin/env python3
"""用固定版本controller/cfg RTL和producer机器码验证依赖阻塞；不替代整机VCS。"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = ("hpu_controller.sv", "hpu_decoder.sv", "hpu_obj_state_table.sv",
         "hpu_sram_allocator.sv", "hpu_cfg_state_regs.sv")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rtl-repository", type=Path, required=True)
    parser.add_argument("--generated-root", type=Path, required=True)
    parser.add_argument("--ref", default="0cfd995302af926aa22767e145aade9fc9d21ca2")
    parser.add_argument("--iverilog", default="iverilog")
    parser.add_argument("--vvp", default="vvp")
    parser.add_argument("--ivl-base", type=Path)
    args = parser.parse_args()
    for tool in (args.iverilog, args.vvp):
        if shutil.which(tool) is None: parser.error(f"required simulator unavailable: {tool}")
    header = (args.generated_root / "include/hpu/inline_asm_mm_delivery.h").read_text()
    defines = []
    for name, macro in (("PADD", "PADD_P0_P0_P1"), ("PMODLD", "PMODLD_0"),
                        ("DLOAD_P0", "DLOAD_P0_POLY"), ("DLOAD_MOD", "DLOAD_P4_MOD")):
        match = re.search(r"#define\s+HPU_INSN_" + macro + r"\s+(?:UINT32_C\()?\s*(0x[0-9a-fA-F]+)", header)
        if match is None: parser.error(f"missing producer encoding: {macro}")
        # 用localparam承接literal后再取位段，兼容SystemVerilog表达式语法。
        defines.append(f"-DTEST_{name}=32'h{int(match[1], 16):08x}")
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="watchdog-dependency-rtl-", dir=build))
    sources = []
    for name in FILES:
        data = subprocess.check_output(["git", "-C", str(args.rtl_repository), "show",
                                        f"{args.ref}:rtl/cpu/SCPU_RTL/RTL/latest/{name}"], text=True)
        source = output / name
        source.write_text(data, encoding="utf-8")
        sources.append(str(source))
    binary = output / "dependencies.vvp"
    command = [args.iverilog, "-g2012", "-s", "watchdog_dependencies_tb"]
    if args.ivl_base: command += ["-B", str(args.ivl_base)]
    subprocess.run(command + defines + ["-o", str(binary)] + sources +
                   [str(ROOT / "scripts/watchdog_dependencies_tb.sv")], check=True)
    log = subprocess.check_output([args.vvp, str(binary)], text=True)
    (output / "result.txt").write_text(log, encoding="utf-8")
    print(log, end="")
    print(f"Dependency RTL evidence: {output} (ref={args.ref})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
