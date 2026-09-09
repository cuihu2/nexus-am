#include <array>
#include <cstdint>
#include <iostream>

#include "assembler.hpp"
#include <hpu/encoding.h>

namespace {

// 只迁移 HPU 计算/控制指令的物理主 opcode；payload 和内部 cmd_kind 均不变。
// 此处独立实现映射，避免仅用导入器同源结果自证正确；DMA custom1 保持原样。
constexpr std::uint32_t target_word(std::uint32_t source_word) {
    return (source_word & 0x7FU) == 0x0BU
        ? (source_word & 0xFFFFFF80U) | 0x5BU : source_word;
}

struct EncodingCheck {
    const char *assembly;
    std::uint32_t expected_word;
};

constexpr std::array<EncodingCheck, 8> kChecks{{
    {"dload x10, x11, p0, 1, 0", HPU_INSN_DLOAD_P0_POLY},
    {"dload x10, x11, p1, 1, 0", HPU_INSN_DLOAD_P1_POLY},
    {"dload x10, x11, p4, 2, 1", HPU_INSN_DLOAD_P4_MOD},
    {"dstore x10, x11, p0, 1", HPU_INSN_DSTORE_P0_RELEASE},
    {"dstore x10, x11, p2, 1", HPU_INSN_DSTORE_P2_RELEASE},
    {"pmodld 0", HPU_INSN_PMODLD_0},
    {"padd p2, p0, p1", HPU_INSN_PADD_P2_P0_P1},
    {"psync", HPU_INSN_PSYNC},
}};

// 固定控制/运算向量覆盖零与非零对象号、模数号，防止误改 bit7 以上字段。
constexpr std::array<EncodingCheck, 6> kControlChecks{{
    {"pmodld 0", 0x6000005BU},
    {"pmodld 255", 0x603FC05BU},
    {"padd p2, p0, p1", 0x0400405BU},
    {"psync", 0x7000005BU},
    {"pfree p0", 0x8000005BU},
    {"pfree p7", 0x81C0005BU},
}};

struct DmaCheck {
    const char *assembly;
    std::uint32_t word;
    std::uint32_t command26;
    unsigned rs1, rs2, object, type_or_release, flag, direction;
};

// main 分支修正后的 DMA 位段；固定数值独立于构建时生成的头文件。
// GPR 恢复标准位置；非对称 x0/x31 抓住互换/截断；STORE 的 rel 在 bit14。
constexpr std::array<DmaCheck, 10> kDmaChecks{{
    {"dload x10, x11, p0, 1, 0", 0x00B5202BU, 0x2016A40U, 10, 11, 0, 1, 0, 0},
    {"dload x10, x11, p1, 1, 0", 0x02B5202BU, 0x2056A40U, 10, 11, 1, 1, 0, 0},
    {"dload x10, x11, p4, 2, 1", 0x08B540ABU, 0x2116A81U, 10, 11, 4, 2, 1, 0},
    {"dstore x10, x11, p0, 1", 0x00B5502BU, 0x2016AA0U, 10, 11, 0, 1, 0, 1},
    {"dstore x10, x11, p2, 1", 0x04B5502BU, 0x2096AA0U, 10, 11, 2, 1, 0, 1},
    {"dload x0, x31, p7, 0, 1", 0x0FF000ABU, 0x21FE001U, 0, 31, 7, 0, 1, 0},
    {"dload x31, x0, p5, 2, 0", 0x0A0FC02BU, 0x2141F80U, 31, 0, 5, 2, 0, 0},
    {"dstore x31, x0, p7, 0", 0x0E0F902BU, 0x21C1F20U, 31, 0, 7, 0, 0, 1},
    {"dstore x0, x31, p1, 1", 0x03F0502BU, 0x207E0A0U, 0, 31, 1, 1, 0, 1},
    {"dload x0, x0, p0, 0, 0", 0x0000002BU, 0x2000000U, 0, 0, 0, 0, 0, 0},
}};

struct StgCheck {
    const char *assembly;
    std::uint32_t word;
    std::uint32_t command26;
    unsigned opcode, pdata, ptwid, stage, mode, flag;
};

// 独立固定向量按 2026-09-05 手册 §3.2 定义，不能仅比较同源生成的头文件。
// 非零 pdata 可抓住源1未复制、twiddle 错放到 [24:22] 的旧版编码。
constexpr std::array<StgCheck, 8> kStgChecks{{
    {"pntt p0, p3, 15, 0, 0", 0x4000FC5BU, 0x08001F8U, 4, 0, 3, 15, 0, 0},
    {"pintt p0, p3, 15, 0, 0", 0x5000FC5BU, 0x0A001F8U, 5, 0, 3, 15, 0, 0},
    {"pntt p2, p3, 15, 0, 0", 0x4480FC5BU, 0x08901F8U, 4, 2, 3, 15, 0, 0},
    {"pintt p5, p1, 7, 2, 1", 0x5B405EDBU, 0x0B680BDU, 5, 5, 1, 7, 2, 1},
    {"pntt p7, p7, 15, 3, 1", 0x4FC1FFDBU, 0x09F83FFU, 4, 7, 7, 15, 3, 1},
    {"pintt p7, p0, 0, 0, 0", 0x5FC0005BU, 0x0BF8000U, 5, 7, 0, 0, 0, 0},
    {"pntt p0, p0, 0, 0, 0", 0x4000005BU, 0x0800000U, 4, 0, 0, 0, 0, 0},
    {"pintt p0, p7, 0, 3, 1", 0x5001C3DBU, 0x0A00387U, 5, 0, 7, 0, 3, 1},
}};

}  // namespace

int main() {
    for (const auto &check : kChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        if (target_word(encoded.word) != check.expected_word) {
            std::cerr << "encoding mismatch: " << check.assembly << '\n';
            return 1;
        }
    }
    for (const auto &check : kControlChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        const auto word = target_word(encoded.word);
        if (word != check.expected_word || (word & 0x7FU) != 0x5BU
            || (word >> 7U) != (encoded.word >> 7U)
            || encoded.command26 != (word >> 7U)
            || (encoded.command26 >> 25U) != 0U) {
            std::cerr << "custom2 control encoding/precode mismatch: "
                      << check.assembly << '\n';
            return 1;
        }
    }
    for (const auto &check : kDmaChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        const auto word = encoded.word;
        const auto operation = check.direction != 0U
            ? check.type_or_release << 1U : check.type_or_release;
        if (target_word(word) != word || word != check.word
            || encoded.command26 != check.command26
            || (word >> 28U) != 0U
            || ((word >> 25U) & 7U) != check.object
            || ((word >> 20U) & 31U) != check.rs2
            || ((word >> 15U) & 31U) != check.rs1
            || ((word >> 13U) & 3U) != operation
            || ((word >> 12U) & 1U) != check.direction
            || ((word >> 8U) & 15U) != 0U
            || ((word >> 7U) & 1U) != check.flag
            || (word & 127U) != 0x2BU
            || encoded.command26 != ((1U << 25U) | (word >> 7U))
            || (encoded.command26 >> 25U) != 1U
            || ((encoded.command26 >> 18U) & 7U) != check.object
            || ((encoded.command26 >> 13U) & 31U) != check.rs2
            || ((encoded.command26 >> 8U) & 31U) != check.rs1) {
            std::cerr << "main DMA encoding/precode mismatch: "
                      << check.assembly << '\n';
            return 1;
        }
    }
    for (const auto &check : kStgChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        const auto word = target_word(encoded.word);
        if (word != check.word || encoded.command26 != check.command26
            || (word >> 28U) != check.opcode
            || ((word >> 25U) & 7U) != check.pdata
            || ((word >> 22U) & 7U) != check.pdata
            || ((word >> 17U) & 31U) != 0U
            || ((word >> 14U) & 7U) != check.ptwid
            || ((word >> 10U) & 15U) != check.stage
            || ((word >> 8U) & 3U) != check.mode
            || ((word >> 7U) & 1U) != check.flag
            || (word & 127U) != 0x5BU
            || (word >> 7U) != (encoded.word >> 7U)
            || encoded.command26 != (word >> 7U)
            || (encoded.command26 >> 25U) != 0U) {
            std::cerr << "manual 2026-09-05 STG mismatch: " << check.assembly << '\n';
            return 1;
        }
    }
    return 0;
}
