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

}  // namespace

int main() {
    for (const auto &check : kChecks) {
        const auto encoded = hpu::assemble_line(check.assembly);
        if (encoded.word != check.expected_word) {
            std::cerr << "encoding mismatch: " << check.assembly << '\n';
            return 1;
        }
    }
    return 0;
}
