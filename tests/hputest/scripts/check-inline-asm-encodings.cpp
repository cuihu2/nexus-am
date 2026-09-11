#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "assembler.hpp"
#include "encoder.hpp"
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
    unsigned opcode, pdst, psrc1, ptwid, stage, mode, flag;
};

// 独立固定向量按本次锁定的 main 编程手册 §3.2：三个对象显式编码。
// 非对称 dst/src 可抓住旧版强制复制 pdata 的错误；mode/flag 极值只测编码，
// 不代表这些保留模式已获准在 IT 中执行。不能仅比较同源生成的头文件。
constexpr std::array<StgCheck, 8> kStgChecks{{
    {"pntt p2, p0, p3, 15, 0, 0", 0x4400FC5BU, 0x08801F8U, 4, 2, 0, 3, 15, 0, 0},
    {"pintt p0, p2, p3, 15, 0, 0", 0x5080FC5BU, 0x0A101F8U, 5, 0, 2, 3, 15, 0, 0},
    {"pntt p2, p5, p3, 15, 0, 0", 0x4540FC5BU, 0x08A81F8U, 4, 2, 5, 3, 15, 0, 0},
    {"pintt p5, p2, p1, 7, 2, 1", 0x5A805EDBU, 0x0B500BDU, 5, 5, 2, 1, 7, 2, 1},
    {"pntt p7, p6, p5, 15, 3, 1", 0x4F817FDBU, 0x09F02FFU, 4, 7, 6, 5, 15, 3, 1},
    {"pintt p7, p0, p1, 0, 0, 0", 0x5E00405BU, 0x0BC0080U, 5, 7, 0, 1, 0, 0, 0},
    {"pntt p0, p7, p1, 0, 0, 0", 0x41C0405BU, 0x0838080U, 4, 0, 7, 1, 0, 0, 0},
    {"pintt p0, p1, p7, 0, 3, 1", 0x5041C3DBU, 0x0A08387U, 5, 0, 1, 7, 0, 3, 1},
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
    for (const auto &check : kControlChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        const auto word = encoded.word;
        if (word != check.expected_word || (word & 0x7FU) != 0x5BU
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
        if (word != check.word
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
        const auto word = encoded.word;
        if (word != check.word || encoded.command26 != check.command26
            || (word >> 28U) != check.opcode
            || ((word >> 25U) & 7U) != check.pdst
            || ((word >> 22U) & 7U) != check.psrc1
            || ((word >> 17U) & 31U) != 0U
            || ((word >> 14U) & 7U) != check.ptwid
            || ((word >> 10U) & 15U) != check.stage
            || ((word >> 8U) & 3U) != check.mode
            || ((word >> 7U) & 1U) != check.flag
            || (word & 127U) != 0x5BU
            || encoded.command26 != (word >> 7U)
            || (encoded.command26 >> 25U) != 0U) {
            std::cerr << "explicit three-object STG mismatch: " << check.assembly << '\n';
            return 1;
        }
    }
    // 当前 producer 必须原生生成 custom2，不能由接收端偷偷补救旧编码。
    try {
        (void)hpu::precode_command26(0x7000000BU);
        std::cerr << "obsolete HPU custom0 word accepted\n";
        return 1;
    } catch (const std::runtime_error &) {
    }
    for (const char *legacy : {"pntt p0, p1, 0, 0, 0",
                               "pintt p2, p3, 11, 0, 0"}) {
        try {
            (void)hpu::assemble_line(legacy);
            std::cerr << "obsolete two-object STG syntax accepted\n";
            return 1;
        } catch (const std::runtime_error &) {
        }
    }
    return 0;
}
