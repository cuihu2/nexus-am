#!/usr/bin/env python3
"""将 HPU_DUMP_RESULTS=1 的 UART 原始日志导出为精确整数结果 CSV。

用法：python3 parse-uart-results.py sim.log --output results.csv
只接收 [HPU][RESULT] 协议 v1，不从其他 printf 推断用例/轮次。
缺少 END、错误序号、重复 block 或统计不一致均报错，不生成部分 CSV。
这不是解密器：没有加密方案、密钥与缩放参数时不能声称完成 FHE 解密。
"""

import argparse
import csv
from pathlib import Path
import sys


PREFIX = "[HPU][RESULT] "
FIELDS = ("case", "phase", "round", "block", "words", "base_address",
          "index", "actual", "expected", "q", "signed_delta")
U32_MAX = (1 << 32) - 1


def number(text, *, signed=False, maximum=U32_MAX):
    """十进制及带 0x 的十六进制；拒绝空白、下划线或模糊的数字形式。"""
    negative = text.startswith("-")
    digits = text[1:] if negative else text
    if not digits or (negative and not signed):
        raise ValueError(f"invalid integer {text!r}")
    if digits.startswith("0x"):
        if negative or not digits[2:] or any(c not in "0123456789abcdefABCDEF"
                                             for c in digits[2:]):
            raise ValueError(f"invalid hex integer {text!r}")
        value = int(text, 16)
    else:
        if any(c not in "0123456789" for c in digits):
            raise ValueError(f"invalid decimal integer {text!r}")
        value = int(text, 10)
    if value > maximum or value < (-maximum if signed else 0):
        raise ValueError(f"integer out of range: {text!r}")
    return value


def name(text):
    if not text or any(ord(c) <= 32 or ord(c) >= 127 or c == "," for c in text):
        raise ValueError(f"invalid context token {text!r}")
    return text


def parse_lines(lines):
    rows = []
    active = None
    seen = set()
    complete = 0
    for lineno, line in enumerate(lines, 1):
        line = line.rstrip("\r\n").lstrip()
        if not line.startswith(PREFIX):
            continue
        parts = line[len(PREFIX):].split(",")
        try:
            kind = parts[0]
            if kind == "BEGIN":
                if active is not None:
                    raise ValueError("BEGIN before previous block END")
                if len(parts) != 10 or parts[1] != "1":
                    raise ValueError("unsupported BEGIN format/version")
                if parts[2] != "full":
                    raise ValueError("brief log is incomplete; use HPU_DUMP_RESULTS=1")
                case_id, phase = name(parts[3]), name(parts[4])
                round_id, block, words, q = (number(x) for x in parts[5:9])
                address = number(parts[9], maximum=(1 << 64) - 1)
                if words == 0:
                    raise ValueError("zero-length result block")
                key = (case_id, phase, round_id, block)
                if key in seen:
                    raise ValueError("duplicate case/phase/round/block")
                seen.add(key)
                active = dict(case=case_id, phase=phase, round=round_id,
                              block=block, words=words, base_address=hex(address),
                              q=q, count=0, mismatch=0, first_bad=-1, maximum=0,
                              q_multiple=0, noncanonical=0)
            elif kind == "DATA":
                if active is None or len(parts) != 6:
                    raise ValueError("DATA outside block or wrong field count")
                index, value, expected, q = (number(x) for x in parts[1:5])
                delta = number(parts[5], signed=True)
                if index != active["count"] or index >= active["words"]:
                    raise ValueError("missing/duplicate/out-of-order DATA index")
                if q != active["q"] or delta != value - expected:
                    raise ValueError("DATA q or signed delta inconsistent")
                outside = q != 0 and value >= q
                bad = value != expected or outside
                active["count"] += 1
                active["maximum"] = max(active["maximum"], abs(delta))
                if bad:
                    if active["first_bad"] == -1:
                        active["first_bad"] = index
                    active["mismatch"] += 1
                    if q and delta and delta % q == 0:
                        active["q_multiple"] += 1
                active["noncanonical"] += int(outside)
                row = {k: active[k] for k in FIELDS[:6]}
                row.update(index=index, actual=value, expected=expected,
                           q=q, signed_delta=delta)
                rows.append(row)
            elif kind == "END":
                if active is None or len(parts) != 9:
                    raise ValueError("END outside block or wrong field count")
                count, emitted, mismatch = (number(x) for x in parts[1:4])
                first_bad = number(parts[4], signed=True)
                maximum, q_multiple, noncanonical = (number(x) for x in parts[5:8])
                wanted = (active["words"], active["count"], active["mismatch"],
                          active["first_bad"], active["maximum"],
                          active["q_multiple"], active["noncanonical"],
                          "FAIL" if active["mismatch"] else "PASS")
                got = (count, emitted, mismatch, first_bad, maximum,
                       q_multiple, noncanonical, parts[8])
                if active["count"] != active["words"] or got != wanted:
                    raise ValueError("truncated DATA or END statistics inconsistent")
                active = None
                complete += 1
            else:
                raise ValueError(f"unknown/error result record {kind!r}")
        except ValueError as error:
            raise ValueError(f"line {lineno}: {error}") from error
    if active is not None:
        raise ValueError("truncated UART log: missing END")
    if not complete:
        raise ValueError("no complete full result blocks in UART log")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="原始 UART/sim.log")
    parser.add_argument("--output", type=Path, required=True, help="完整结果 CSV")
    args = parser.parse_args()
    if args.log.resolve() == args.output.resolve():
        parser.error("input log and output CSV must be different files")
    try:
        with args.log.open(encoding="utf-8", errors="replace") as source:
            rows = parse_lines(source)
        # 完成协议检查后才创建输出，失败时不覆盖已有 CSV。
        with args.output.open("w", encoding="utf-8", newline="") as destination:
            writer = csv.DictWriter(destination, fieldnames=FIELDS)
            writer.writeheader()
            writer.writerows(rows)
    except (OSError, ValueError) as error:
        parser.exit(1, f"UART result export failed: {error}\n")
    print(f"Exported {len(rows)} exact-integer result words to {args.output}")


if __name__ == "__main__":
    main()
