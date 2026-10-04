#include "assembler.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc,char **argv) {
    try {
        if (argc!=3) throw std::runtime_error("usage: encode-program ASM TABLE");
        std::ifstream input(argv[1]); std::ofstream output(argv[2]);
        if (!input || !output) throw std::runtime_error("missing input/output");
        const std::string text((std::istreambuf_iterator<char>(input)),{});
        const auto encoded=hpu::assemble_source(text);
        output<<"word_hex\tcommand_hex\tnormalized_asm\n";
        for (const auto &instruction:encoded)
            output<<hpu::format_word_hex(instruction.word)<<'\t'
                  <<hpu::format_command26_hex(instruction.command26)<<'\t'
                  <<instruction.normalized_asm<<'\n';
    } catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
