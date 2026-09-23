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
    {"HPU_SEAL_INSN_DLOAD_P0_POLY", "dload x10, x11, p0, 1, 0"},
    {"HPU_SEAL_INSN_DLOAD_P1_POLY", "dload x10, x11, p1, 1, 0"},
    {"HPU_SEAL_INSN_DLOAD_P4_MOD", "dload x10, x11, p4, 2, 1"},
    {"HPU_SEAL_INSN_DSTORE_P2_RELEASE", "dstore x10, x11, p2, 1"},
    {"HPU_SEAL_INSN_PMODLD_0", "pmodld 0"},
    {"HPU_SEAL_INSN_PMODLD_1", "pmodld 1"},
    {"HPU_SEAL_INSN_PMODLD_2", "pmodld 2"},
    {"HPU_SEAL_INSN_PMODLD_3", "pmodld 3"},
    {"HPU_SEAL_INSN_PADD_P2_P0_P1", "padd p2, p0, p1"},
    {"HPU_SEAL_INSN_PFREE_P0", "pfree p0"},
    {"HPU_SEAL_INSN_PFREE_P1", "pfree p1"},
    {"HPU_SEAL_INSN_PFREE_P4", "pfree p4"},
    {"HPU_SEAL_INSN_PSYNC", "psync"},
};

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 2) {
            std::cerr << "usage: generate-hpu-seal-hadd-encodings OUTPUT\n";
            return 2;
        }
        std::ofstream output(argv[1]);
        if (!output) {
            throw std::runtime_error("cannot open output file");
        }
        output << "macro_name\tword_hex\tnormalized_asm\n";
        for (const auto& request : kRequests) {
            const auto encoded = hpu::assemble_source(std::string(request.assembly) + "\n");
            if (encoded.size() != 1U) {
                throw std::runtime_error(
                    std::string("expected one encoded instruction for ") + request.assembly);
            }
            output << request.macro_name << "\t0x" << std::uppercase << std::hex
                   << std::setw(8) << std::setfill('0')
                   << static_cast<std::uint32_t>(encoded.front().word) << std::dec << "\t"
                   << encoded.front().normalized_asm << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HPU_SEAL HADD encoding generation failed: " << error.what() << '\n';
        return 1;
    }
}
