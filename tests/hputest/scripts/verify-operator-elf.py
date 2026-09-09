#!/usr/bin/env python3
"""核对算子 ELF 的实际指令流和只读数据，防止源码更新但仍发布旧镜像。"""

import argparse
import os
from pathlib import Path
import re
import struct
import subprocess


def symbol_bytes(elf, symbol):
    listing = subprocess.check_output(
        [os.environ.get("CROSS_COMPILE", "riscv64-linux-gnu-") + "nm",
         "-S", "--defined-only", str(elf)], text=True)
    matches = re.findall(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[Rr]\s+" +
                         re.escape(symbol) + r"$", listing, re.M)
    if len(matches) != 1:
        raise ValueError(f"missing/duplicate read-only ELF symbol: {symbol}")
    address, size = (int(value, 16) for value in matches[0])
    blob = elf.read_bytes()
    if blob[:6] != b"\x7fELF\x02\x01":
        raise ValueError("expected little-endian ELF64")
    table = struct.unpack_from("<Q", blob, 40)[0]
    stride, count = struct.unpack_from("<HH", blob, 58)
    if stride != 64 or not size:
        raise ValueError("invalid section table/symbol size")
    for index in range(count):
        section = struct.unpack_from("<IIQQQQIIQQ", blob, table + stride * index)
        _, kind, flags, start, offset, length, *_ = section
        if kind == 1 and flags & 2 and not flags & 1 and \
                start <= address and address + size <= start + length:
            first = offset + address - start
            result = blob[first:first + size]
            if len(result) != size:
                raise ValueError("truncated section")
            return result
    raise ValueError(f"symbol is not in a read-only PROGBITS section: {symbol}")


def stream(disassembly, function):
    active = False
    found = []
    for line in disassembly.splitlines():
        label = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if label:
            active = label[1] == function
        word = re.match(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})(?:\s|$)", line)
        if active and word and int(word[1], 16) & 0x7f in (0x0b, 0x2b, 0x5b):
            found.append(int(word[1], 16))
    return found


def verify(delivery, elf, disassembly, operator):
    expected = [int(word, 2) for word in (delivery / f"{operator}.inst32").read_text().split()]
    actual = stream(disassembly.read_text(), f"hpu_program_{operator}")
    if not expected or actual != expected:
        raise ValueError(f"{operator}: linked instruction sequence differs from delivery")
    if expected[-1] != 0x7000005b or expected.count(0x7000005b) != 1:
        raise ValueError("expected exactly one terminal PSYNC")
    fixtures = [(f"transform_{operator}_image", "window.u32.bin"),
                (f"transform_{operator}_golden", "golden.u32.bin")]
    if operator == "bconv":
        fixtures = [("bconv_window", "window.u32.bin"),
                    ("bconv_golden", "golden_p.u32.bin"),
                    ("bconv_normalized", "normalized_cpu.u32.bin")]
    for symbol, filename in fixtures:
        if symbol_bytes(elf, symbol) != (delivery / filename).read_bytes():
            raise ValueError(f"{operator}: linked {symbol} differs from validated delivery")
    print(f"[hputest] {operator}: linked {len(actual)} commands, image/golden bytes verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--delivery", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--disassembly", required=True, type=Path)
    parser.add_argument("--operator", required=True, choices=("ntt", "intt", "bconv"))
    args = parser.parse_args()
    verify(args.delivery, args.elf, args.disassembly, args.operator)
