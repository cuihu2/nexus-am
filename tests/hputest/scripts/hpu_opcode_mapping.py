"""AM 接收层：仅将上游 HPU 控制/计算指令的主 opcode 映射到 custom-2。"""

import re


SOURCE_OPCODE = 0x0B
TARGET_OPCODE = 0x5B
DMA_OPCODE = 0x2B

# 只处理生成 C 中的完整 .word 指令标记，不替换数据、地址或寄存器绑定。
_WORD_RE = re.compile(
    r"(?<![\w.])(?P<prefix>\.word[ \t]+0x)"
    r"(?P<word>[0-9a-fA-F]{8})(?![0-9a-zA-Z_])"
)


def map_word(word: int) -> int:
    """保留 inst[31:7]；DMA 原样通过，未知或已映射的 opcode 拒绝通过。"""
    if not isinstance(word, int) or isinstance(word, bool) or not 0 <= word <= 0xFFFFFFFF:
        raise RuntimeError(f"HPU instruction must be a uint32 value: {word!r}")

    opcode = word & 0x7F
    if opcode == SOURCE_OPCODE:
        return (word & ~0x7F) | TARGET_OPCODE
    if opcode == DMA_OPCODE:
        return word
    raise RuntimeError(
        f"unexpected HPU source opcode 0x{opcode:02X} in 0x{word:08X}; "
        "expected unmapped custom-0 (0x0B) or DMA (0x2B)"
    )


def map_c(source: str) -> str:
    """映射生成 C 的 .word 字面量；所有其他 C 文本保持原样。"""
    def replace(match: re.Match) -> str:
        word = int(match.group("word"), 16)
        mapped = map_word(word)
        # DMA 连字面量的大小写也不改变，便于逐字比对上游输出。
        if mapped == word:
            return match.group(0)
        return match.group("prefix") + f"{mapped:08X}"

    return _WORD_RE.sub(replace, source)
