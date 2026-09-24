#include "assembler.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>

// 用固定子模块的真实编码器重新解析 producer ASM，不在 AM 维护第二套编码表。
int main(int argc, char **argv) {
    try {
        if (argc != 4) throw std::runtime_error("usage: verify-ckks-encoding ASM INST32 CMD26");
        std::ifstream source(argv[1]), words(argv[2]), commands(argv[3]);
        if (!source || !words || !commands) throw std::runtime_error("missing delivery");
        const std::string text((std::istreambuf_iterator<char>(source)), {});
        const std::regex quoted("\"([^\"]*)\\\\n\\\\t\"");
        std::string assembly;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), quoted);
             it != std::sregex_iterator(); ++it) assembly += (*it)[1].str() + "\n";
        const auto encoded = hpu::assemble_source(assembly);
        if (encoded.empty()) throw std::runtime_error("empty ASM");
        std::string word, command;
        for (const auto &instruction : encoded) {
            if (!(words >> word) || !(commands >> command) || word.size() != 32 ||
                command.size() != 26 ||
                std::stoul(word, nullptr, 2) != instruction.word ||
                std::stoul(command, nullptr, 2) != instruction.command26)
                throw std::runtime_error("ASM/INST32/CMD26 encoding mismatch");
        }
        if (words >> word || commands >> command) throw std::runtime_error("extra words");
        std::cout << "CKKS producer encoding verified: " << encoded.size() << " instructions\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
