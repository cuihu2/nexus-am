#include <array>
#include <cstdint>
#include <iostream>

#include "assembler.hpp"
#include <hpu/encoding.h>

namespace {

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

struct DmaCheck {
    const char *assembly;
    std::uint32_t word;
    std::uint32_t command26;
    unsigned rs1, rs2, object, type_or_release, flag, direction;
};

// main 分支修正后的 DMA 位段；固定数值独立于构建时生成的头文件。
// 非对称 x0/x31 抓住 GPR 互换/截断；STORE 的 rel 在 bit9，不是 LOAD 的 type。
constexpr std::array<DmaCheck, 10> kDmaChecks{{
    {"dload x10, x11, p0, 1, 0", 0x5A80012BU, 0x2B50002U, 10, 11, 0, 1, 0, 0},
    {"dload x10, x11, p1, 1, 0", 0x5A80052BU, 0x2B5000AU, 10, 11, 1, 1, 0, 0},
    {"dload x10, x11, p4, 2, 1", 0x5A82122BU, 0x2B50424U, 10, 11, 4, 2, 1, 0},
    {"dstore x10, x11, p0, 1", 0x5A8002ABU, 0x2B50005U, 10, 11, 0, 1, 0, 1},
    {"dstore x10, x11, p2, 1", 0x5A800AABU, 0x2B50015U, 10, 11, 2, 1, 0, 1},
    {"dload x0, x31, p7, 0, 1", 0xF8021C2BU, 0x3F00438U, 0, 31, 7, 0, 1, 0},
    {"dload x31, x0, p5, 2, 0", 0x07C0162BU, 0x20F802CU, 31, 0, 5, 2, 0, 0},
    {"dstore x31, x0, p7, 0", 0x07C01CABU, 0x20F8039U, 31, 0, 7, 0, 0, 1},
    {"dstore x0, x31, p1, 1", 0xF80006ABU, 0x3F0000DU, 0, 31, 1, 1, 0, 1},
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
    {"pntt p0, p3, 15, 0, 0", 0x4000FC0BU, 0x08001F8U, 4, 0, 3, 15, 0, 0},
    {"pintt p0, p3, 15, 0, 0", 0x5000FC0BU, 0x0A001F8U, 5, 0, 3, 15, 0, 0},
    {"pntt p2, p3, 15, 0, 0", 0x4480FC0BU, 0x08901F8U, 4, 2, 3, 15, 0, 0},
    {"pintt p5, p1, 7, 2, 1", 0x5B405E8BU, 0x0B680BDU, 5, 5, 1, 7, 2, 1},
    {"pntt p7, p7, 15, 3, 1", 0x4FC1FF8BU, 0x09F83FFU, 4, 7, 7, 15, 3, 1},
    {"pintt p7, p0, 0, 0, 0", 0x5FC0000BU, 0x0BF8000U, 5, 7, 0, 0, 0, 0},
    {"pntt p0, p0, 0, 0, 0", 0x4000000BU, 0x0800000U, 4, 0, 0, 0, 0, 0},
    {"pintt p0, p7, 0, 3, 1", 0x5001C38BU, 0x0A00387U, 5, 0, 7, 0, 3, 1},
}};

}  // namespace

int main() {
    for (const auto &check : kChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        if (encoded.word != check.expected_word) {
            std::cerr << "encoding mismatch: " << check.assembly << '\n';
            return 1;
        }
    }
    for (const auto &check : kDmaChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        const auto word = encoded.word;
        const auto operation = check.direction != 0U
            ? check.type_or_release << 1U : check.type_or_release;
        if (word != check.word || encoded.command26 != check.command26
            || ((word >> 27U) & 31U) != check.rs2
            || ((word >> 22U) & 31U) != check.rs1
            || ((word >> 18U) & 15U) != 0U
            || ((word >> 17U) & 1U) != check.flag
            || ((word >> 13U) & 15U) != 0U
            || ((word >> 10U) & 7U) != check.object
            || ((word >> 8U) & 3U) != operation
            || ((word >> 7U) & 1U) != check.direction
            || (word & 127U) != 0x2BU
            || encoded.command26 != ((1U << 25U) | (word >> 7U))
            || (encoded.command26 >> 25U) != 1U
            || ((encoded.command26 >> 20U) & 31U) != check.rs2
            || ((encoded.command26 >> 15U) & 31U) != check.rs1) {
            std::cerr << "main DMA encoding/precode mismatch: "
                      << check.assembly << '\n';
            return 1;
        }
    }
    for (const auto &check : kStgChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        const auto word = encoded.word;
        if (word != check.word || encoded.command26 != check.command26
            || (word >> 28U) != check.opcode
            || ((word >> 25U) & 7U) != check.pdata
            || ((word >> 22U) & 7U) != check.pdata
            || ((word >> 17U) & 31U) != 0U
            || ((word >> 14U) & 7U) != check.ptwid
            || ((word >> 10U) & 15U) != check.stage
            || ((word >> 8U) & 3U) != check.mode
            || ((word >> 7U) & 1U) != check.flag
            || (word & 127U) != 0x0BU
            || encoded.command26 != (word >> 7U)
            || (encoded.command26 >> 25U) != 0U) {
            std::cerr << "manual 2026-09-05 STG mismatch: " << check.assembly << '\n';
            return 1;
        }
    }
    return 0;
}
