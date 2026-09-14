#!/usr/bin/env python3
"""静态验证静默 RV64 ELF；不执行 CPU/HPU，也不替代功能或 VCS 回归。"""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import struct
import subprocess


FORBIDDEN = frozenset({
    "printf", "printf_", "atomic_printf_", "atomic_printf", "vprintf_", "vprintf",
    "puts", "putchar", "fctprintf", "_out_char", "__am_uartlite_putchar",
    "__am_16550_putchar", "__am_init_uartlite", "__am_init_16550",
})
BRIDGES = frozenset({"_putc", "_putchar"})
FUNCTION_TYPES = frozenset("TtWw")


@dataclass(frozen=True)
class Symbol:
    address: int
    size: int
    kind: str


@dataclass(frozen=True)
class Instruction:
    address: int
    encoding: str
    mnemonic: str
    operands: str


def _run(arguments):
    try:
        return subprocess.run(arguments, check=True, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", None) or str(error)
        raise ValueError(f"tool failed: {arguments[0]}: {detail.strip()}") from error


def _symbols(output):
    symbols = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 4:
            address, size, kind, name = fields
        elif len(fields) == 3:
            address, kind, name = fields
            size = "0"
        else:
            raise ValueError(f"unrecognized nm row: {line!r}")
        try:
            symbol = Symbol(int(address, 16), int(size, 16), kind)
        except ValueError as error:
            raise ValueError(f"invalid nm address/size: {line!r}") from error
        if len(kind) != 1 or name in symbols:
            raise ValueError(f"invalid/duplicate nm symbol: {name}")
        symbols[name] = symbol
    return symbols


def _instructions(output):
    instructions = {}
    pattern = re.compile(r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{4}|[0-9a-fA-F]{8})"
                         r"\s+(\S+)(?:\s+(.*))?$")
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            address, encoding, mnemonic, operands = match.groups()
            address = int(address, 16)
            if address in instructions:
                raise ValueError(f"duplicate instruction address: 0x{address:x}")
            instructions[address] = Instruction(
                address, encoding.lower(), mnemonic,
                (operands or "").split("#", 1)[0].strip())
    return instructions


def _body(name, symbols, instructions):
    symbol = symbols.get(name)
    if symbol is None or symbol.kind not in FUNCTION_TYPES or symbol.size <= 0:
        raise ValueError(f"missing sized function: {name}")
    cursor = symbol.address
    end = cursor + symbol.size
    body = []
    while cursor < end:
        instruction = instructions.get(cursor)
        if instruction is None:
            raise ValueError(f"undecoded instruction in {name} at 0x{cursor:x}")
        body.append(instruction)
        cursor += len(instruction.encoding) // 2
    if cursor != end:
        raise ValueError(f"instruction crosses function boundary: {name}")
    return body


def _inert_body(name, body, wrappers, symbols, bridge=False):
    """只接受无输出桩及已验证桩的薄桥，不用符号存在与否猜测 UART 行为。"""
    depth = 0
    finished = False
    for index, instruction in enumerate(body):
        opcode = instruction.mnemonic
        operands = re.sub(r"\s+", "", instruction.operands)
        if finished:
            raise ValueError(f"instructions after return/tail in {name}")
        if opcode == "ret" and not operands:
            if depth != 0:
                raise ValueError(f"unbalanced stack in {name}")
            finished = True
            continue
        if opcode == "nop" and not operands:
            continue
        if opcode == "li" and operands == "a0,0":
            continue
        if opcode == "mv" and operands in ("a0,zero", "a0,a0"):
            continue
        if opcode in ("sext.w", "zext.w") and operands == "a0,a0":
            continue
        adjustment = re.fullmatch(r"sp,sp,(-?(?:0x[0-9a-fA-F]+|[0-9]+))", operands)
        if opcode in ("addi", "add") and adjustment:
            amount = int(adjustment.group(1), 0)
            depth -= amount
            if amount % 16 or not 0 <= depth <= 4096:
                raise ValueError(f"invalid stack adjustment in {name}")
            continue
        access = re.fullmatch(r"([a-z][a-z0-9]*),((?:0x[0-9a-fA-F]+|[0-9]+))\(sp\)",
                              operands)
        widths = {"sd": 8, "sw": 4, "sh": 2, "sb": 1,
                  "ld": 8, "lw": 4, "lwu": 4, "lh": 2, "lhu": 2,
                  "lb": 1, "lbu": 1}
        if opcode in widths and access:
            offset = int(access.group(2), 0)
            register = access.group(1)
            if not re.fullmatch(r"zero|ra|[ast][0-9]+", register):
                raise ValueError(f"invalid stack register in {name}: {register}")
            if offset + widths[opcode] > depth:
                raise ValueError(f"stack access outside allocated frame in {name}")
            continue
        if bridge and opcode in ("j", "jal", "call", "tail"):
            target = re.search(r"(?:^|[,\s])([0-9a-fA-F]+)\s+<([^>]+)>$",
                               instruction.operands)
            if target and target.group(2) in wrappers:
                destination = symbols[target.group(2)].address
                if int(target.group(1), 16) == destination:
                    if opcode in ("j", "tail"):
                        if depth != 0:
                            raise ValueError(f"unbalanced tail bridge: {name}")
                        finished = True
                    continue
        raise ValueError(f"non-inert instruction in {name}: "
                         f"{opcode} {instruction.operands}")
    if not finished:
        raise ValueError(f"missing return/tail in {name}")


def verify(elf, cross="riscv64-linux-gnu-"):
    """验证单个 ELF，失败抛出 ValueError；仅允许空 UART 桩，不证明 IT PASS。"""
    elf = Path(elf)
    try:
        with elf.open("rb") as stream:
            header = stream.read(64)
    except OSError as error:
        raise ValueError(f"cannot read ELF: {elf}: {error}") from error
    if len(header) != 64 or header[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("expected little-endian ELF64 version 1")
    kind, machine, version, entry = struct.unpack_from("<HHIQ", header, 16)
    if kind != 2 or machine != 243 or version != 1 or struct.unpack_from("<H", header, 52)[0] != 64:
        raise ValueError("expected executable RISC-V ELF64")
    if entry != 0x80000000:
        raise ValueError(f"unexpected ELF entry: 0x{entry:x}")
    symbols = _symbols(_run([cross + "nm", "-S", "--defined-only", str(elf)]))
    forbidden = sorted(FORBIDDEN.intersection(symbols))
    if forbidden:
        raise ValueError("real formatter/UART symbols retained: " + ", ".join(forbidden))
    instructions = _instructions(_run([cross + "objdump", "-d", str(elf)]))
    _body("main", symbols, instructions)
    halt = _body("_halt", symbols, instructions)
    if not any(instruction.encoding == "0005006b" for instruction in halt):
        raise ValueError("_halt omits original stop instruction 0005006b")
    wrappers = {name for name in symbols if name.startswith("__wrap_")}
    for name in sorted(wrappers):
        _inert_body(name, _body(name, symbols, instructions), wrappers, symbols)
    for name in sorted(BRIDGES.intersection(symbols)):
        _inert_body(name, _body(name, symbols, instructions), wrappers, symbols, bridge=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--cross-compile", default="riscv64-linux-gnu-")
    arguments = parser.parse_args()
    try:
        verify(arguments.elf, arguments.cross_compile)
    except ValueError as error:
        parser.exit(1, f"ERROR: silent ELF validation: {error}\n")
    print(f"[hputest] silent ELF static validation PASS: {arguments.elf}")


if __name__ == "__main__":
    main()
