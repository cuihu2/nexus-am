#include "assembler.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct EncodingRequest {
    const char* macro_name;
    const char* assembly;
};

constexpr EncodingRequest kRequests[] = {
    {"HPU_INSN_DLOAD_P0_POLY", "dload x10, x11, p0, 1, 0"},
    {"HPU_INSN_DLOAD_P1_POLY", "dload x10, x11, p1, 1, 0"},
    {"HPU_INSN_DLOAD_P2_POLY", "dload x10, x11, p2, 1, 0"},
    {"HPU_INSN_DLOAD_P3_POLY", "dload x10, x11, p3, 1, 0"},
    {"HPU_INSN_DLOAD_P4_POLY", "dload x10, x11, p4, 1, 0"},
    {"HPU_INSN_DLOAD_P5_POLY", "dload x10, x11, p5, 1, 0"},
    {"HPU_INSN_DLOAD_P6_POLY", "dload x10, x11, p6, 1, 0"},
    {"HPU_INSN_DLOAD_P7_POLY", "dload x10, x11, p7, 1, 0"},
    {"HPU_INSN_DLOAD_P4_MOD", "dload x10, x11, p4, 2, 1"},
    {"HPU_INSN_DSTORE_P0_RELEASE", "dstore x10, x11, p0, 1"},
    {"HPU_INSN_DSTORE_P1_RELEASE", "dstore x10, x11, p1, 1"},
    {"HPU_INSN_DSTORE_P2_RELEASE", "dstore x10, x11, p2, 1"},
    {"HPU_INSN_DSTORE_P3_RELEASE", "dstore x10, x11, p3, 1"},
    {"HPU_INSN_DSTORE_P4_RELEASE", "dstore x10, x11, p4, 1"},
    {"HPU_INSN_DSTORE_P5_RELEASE", "dstore x10, x11, p5, 1"},
    {"HPU_INSN_DSTORE_P6_RELEASE", "dstore x10, x11, p6, 1"},
    {"HPU_INSN_DSTORE_P7_RELEASE", "dstore x10, x11, p7, 1"},
    {"HPU_INSN_DSTORE_P0_KEEP", "dstore x10, x11, p0, 0"},
    {"HPU_INSN_DSTORE_P2_KEEP", "dstore x10, x11, p2, 0"},
    {"HPU_INSN_DSTORE_P7_KEEP", "dstore x10, x11, p7, 0"},
    {"HPU_INSN_PMODLD_0", "pmodld 0"},
    {"HPU_INSN_PMODLD_1", "pmodld 1"},
    {"HPU_INSN_PMODLD_2", "pmodld 2"},
    {"HPU_INSN_PMODLD_3", "pmodld 3"},
    {"HPU_INSN_PMODLD_4", "pmodld 4"},
    {"HPU_INSN_PMODLD_5", "pmodld 5"},
    {"HPU_INSN_PMODLD_6", "pmodld 6"},
    {"HPU_INSN_PADD_P2_P0_P1", "padd p2, p0, p1"},
    {"HPU_INSN_PADD_P0_P0_P1", "padd p0, p0, p1"},
    {"HPU_INSN_PADD_P2_P2_P0", "padd p2, p2, p0"},
    {"HPU_INSN_PADD_P2_P2_P1", "padd p2, p2, p1"},
    {"HPU_INSN_PSUB_P2_P0_P1", "psub p2, p0, p1"},
    {"HPU_INSN_PSUB_P0_P0_P1", "psub p0, p0, p1"},
    {"HPU_INSN_PMUL_P2_P0_P1", "pmul p2, p0, p1"},
    {"HPU_INSN_PMUL_P0_P0_P1", "pmul p0, p0, p1"},
    {"HPU_INSN_PMUL_P2_P2_P0", "pmul p2, p2, p0"},
    {"HPU_INSN_PMUL_P2_P2_P1", "pmul p2, p2, p1"},
    {"HPU_INSN_PMUL_IMM0_P2_P0", "pmul p2, p0, 0"},
    {"HPU_INSN_PMUL_IMM7_P2_P0", "pmul p2, p0, 7"},
    {"HPU_INSN_PMUL_IMM255_P2_P0", "pmul p2, p0, 255"},
    {"HPU_INSN_PMAC_P2_P0_P1", "pmac p2, p0, p1"},
    {"HPU_INSN_PMAC_IMM0_P2_P0", "pmac p2, p0, 0"},
    {"HPU_INSN_PMAC_IMM1_P2_P0", "pmac p2, p0, 1"},
    {"HPU_INSN_PMAC_IMM255_P2_P0", "pmac p2, p0, 255"},
    {"HPU_INSN_PNTT_STAGE0", "pntt p0, p1, 0, 0, 0"},
    {"HPU_INSN_PNTT_STAGE1", "pntt p0, p1, 1, 0, 0"},
    {"HPU_INSN_PNTT_STAGE2", "pntt p0, p1, 2, 0, 0"},
    {"HPU_INSN_PNTT_STAGE3", "pntt p0, p1, 3, 0, 0"},
    {"HPU_INSN_PNTT_STAGE4", "pntt p0, p1, 4, 0, 0"},
    {"HPU_INSN_PNTT_STAGE5", "pntt p0, p1, 5, 0, 0"},
    {"HPU_INSN_PNTT_STAGE6", "pntt p0, p1, 6, 0, 0"},
    {"HPU_INSN_PNTT_STAGE7", "pntt p0, p1, 7, 0, 0"},
    {"HPU_INSN_PNTT_STAGE8", "pntt p0, p1, 8, 0, 0"},
    {"HPU_INSN_PNTT_STAGE9", "pntt p0, p1, 9, 0, 0"},
    {"HPU_INSN_PNTT_STAGE10", "pntt p0, p1, 10, 0, 0"},
    {"HPU_INSN_PNTT_STAGE11", "pntt p0, p1, 11, 0, 0"},
    {"HPU_INSN_PINTT_STAGE0", "pintt p0, p1, 0, 0, 0"},
    {"HPU_INSN_PINTT_STAGE1", "pintt p0, p1, 1, 0, 0"},
    {"HPU_INSN_PINTT_STAGE2", "pintt p0, p1, 2, 0, 0"},
    {"HPU_INSN_PINTT_STAGE3", "pintt p0, p1, 3, 0, 0"},
    {"HPU_INSN_PINTT_STAGE4", "pintt p0, p1, 4, 0, 0"},
    {"HPU_INSN_PINTT_STAGE5", "pintt p0, p1, 5, 0, 0"},
    {"HPU_INSN_PINTT_STAGE6", "pintt p0, p1, 6, 0, 0"},
    {"HPU_INSN_PINTT_STAGE7", "pintt p0, p1, 7, 0, 0"},
    {"HPU_INSN_PINTT_STAGE8", "pintt p0, p1, 8, 0, 0"},
    {"HPU_INSN_PINTT_STAGE9", "pintt p0, p1, 9, 0, 0"},
    {"HPU_INSN_PINTT_STAGE10", "pintt p0, p1, 10, 0, 0"},
    {"HPU_INSN_PINTT_STAGE11", "pintt p0, p1, 11, 0, 0"},
    {"HPU_INSN_PFREE_P0", "pfree p0"},
    {"HPU_INSN_PFREE_P1", "pfree p1"},
    {"HPU_INSN_PFREE_P2", "pfree p2"},
    {"HPU_INSN_PFREE_P3", "pfree p3"},
    {"HPU_INSN_PFREE_P4", "pfree p4"},
    {"HPU_INSN_PFREE_P5", "pfree p5"},
    {"HPU_INSN_PFREE_P6", "pfree p6"},
    {"HPU_INSN_PFREE_P7", "pfree p7"},
    {"HPU_INSN_PSYNC", "psync"},
};

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: generate-inline-asm-encodings OUTPUT\n";
            return 2;
        }

        std::ofstream output(argv[1]);
        if (!output) {
            throw std::runtime_error("cannot open output file");
        }

        output << "macro_name\tword_hex\tnormalized_asm\n";
        for (const auto& request : kRequests) {
            const auto encoded = hpu::assemble_source(
                std::string(request.assembly) + "\n");
            if (encoded.size() != 1U) {
                throw std::runtime_error(
                    std::string("expected one encoded instruction for ") +
                    request.assembly);
            }
            output << request.macro_name << "\t0x" << std::uppercase
                   << std::hex << std::setw(8) << std::setfill('0')
                   << static_cast<std::uint32_t>(encoded.front().word)
                   << std::dec << "\t" << encoded.front().normalized_asm
                   << '\n';
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "encoding-header input generation failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
