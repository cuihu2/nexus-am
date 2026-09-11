"""AM 接收层：校验新 producer 的原生 custom-2/custom-1，指令不再重写。"""

import re


SOURCE_OPCODE = 0x5B
TARGET_OPCODE = 0x5B
DMA_OPCODE = 0x2B

# 只处理生成 C 中的完整 .word 指令标记，不替换数据、地址或寄存器绑定。
_WORD_RE = re.compile(
    r"(?<![\w.])(?P<prefix>\.word[ \t]+0x)"
    r"(?P<word>[0-9a-fA-F]{8})(?![0-9a-zA-Z_])"
)


def map_word(word: int) -> int:
    """所有位原样保留；旧 custom-0 或未知 opcode 必须拒绝，不能混用旧交付。"""
    if not isinstance(word, int) or isinstance(word, bool) or not 0 <= word <= 0xFFFFFFFF:
        raise RuntimeError(f"HPU instruction must be a uint32 value: {word!r}")

    opcode = word & 0x7F
    if opcode in (SOURCE_OPCODE, DMA_OPCODE):
        return word
    raise RuntimeError(
        f"unexpected HPU source opcode 0x{opcode:02X} in 0x{word:08X}; "
        "expected native custom-2 (0x5B) or DMA (0x2B)"
    )


def map_c(source: str) -> str:
    """检查生成 C 的 .word；保留原文本，包括大小写、数据和寄存器绑定。"""
    def replace(match: re.Match) -> str:
        word = int(match.group("word"), 16)
        mapped = map_word(word)
        # DMA 连字面量的大小写也不改变，便于逐字比对上游输出。
        if mapped == word:
            return match.group(0)
        return match.group("prefix") + f"{mapped:08X}"

    return _WORD_RE.sub(replace, source)
