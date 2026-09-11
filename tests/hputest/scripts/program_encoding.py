"""用本次从固定 inline-asm 源码重新编译的编码表核对完整程序的每条机器码。"""

import csv


def check_program(program, words, encodings):
    with encodings.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != ["macro_name", "word_hex", "normalized_asm"]:
            raise ValueError("invalid producer encoder table")
        expected = {}
        for row in reader:
            mnemonic = row["normalized_asm"]
            word = int(row["word_hex"], 0)
            if mnemonic in expected or word & 0x7f not in (0x5b, 0x2b):
                raise ValueError("duplicate or non-source word in producer encoder table")
            expected[mnemonic] = word
    if len(program) != len(words):
        raise ValueError("program length differs from instruction words")
    for index, (mnemonic, word) in enumerate(zip(program, words)):
        if expected.get(mnemonic) != word:
            raise ValueError(f"instruction {index}: word 0x{word:08X} does not match "
                             f"fresh producer encoder for {mnemonic!r}")
