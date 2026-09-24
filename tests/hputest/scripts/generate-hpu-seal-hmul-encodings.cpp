#include "assembler.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    try {
        if (argc != 3) {
            std::cerr << "usage: generate-hpu-seal-hmul-encodings INPUT_ASM OUTPUT\n";
            return 2;
        }
        std::ifstream input(argv[1]);
        if (!input) {
            throw std::runtime_error("cannot open input assembly");
        }
        std::ostringstream raw;
        raw << input.rdbuf();
        const std::regex quoted(R"re("(.*?) \\n\\t")re");
        std::string source;
        const std::string text = raw.str();
        for (auto match = std::sregex_iterator(text.begin(), text.end(), quoted);
             match != std::sregex_iterator(); ++match) {
            source += (*match)[1].str() + '\n';
        }
        const auto encoded = hpu::assemble_source(source);
        if (encoded.empty()) {
            throw std::runtime_error("input contains no assembly instructions");
        }
        std::map<std::string, std::uint32_t> unique;
        for (const auto& instruction : encoded) {
            const auto [entry, inserted] = unique.emplace(
                instruction.normalized_asm, static_cast<std::uint32_t>(instruction.word));
            if (!inserted && entry->second != instruction.word) {
                throw std::runtime_error("one normalized instruction has multiple encodings");
            }
        }

        std::ofstream output(argv[2]);
        if (!output) {
            throw std::runtime_error("cannot open output table");
        }
        output << "macro_name\tword_hex\tnormalized_asm\n";
        std::size_t index = 0;
        for (const auto& [assembly, word] : unique) {
            output << "HPU_SEAL_HMUL_" << std::dec << index++ << "\t0x"
                   << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
                   << word << std::dec << "\t" << assembly << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HPU_SEAL HMUL encoding generation failed: " << error.what() << '\n';
        return 1;
    }
}
