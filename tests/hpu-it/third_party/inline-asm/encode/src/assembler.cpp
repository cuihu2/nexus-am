#include "assembler.hpp"

#include <iomanip>
#include <sstream>

#include "encoder.hpp"
#include "parser.hpp"

namespace hpu {

EncodedInstruction assemble_line(std::string_view line) {
    EncodedInstruction encoded;
    encoded.instruction = parse_instruction_line(line);
    encoded.word = encode_instruction(encoded.instruction);
    encoded.command26 = precode_command26(encoded.word);
    encoded.normalized_asm = to_string(encoded.instruction);
    return encoded;
}

std::vector<EncodedInstruction> assemble_source(std::string_view source) {
    std::vector<EncodedInstruction> encoded;
    for (const auto& instruction : parse_source(source)) {
        const std::uint32_t word = encode_instruction(instruction);
        encoded.push_back(
            {instruction, word, precode_command26(word), to_string(instruction)});
    }
    return encoded;
}

std::string format_word_hex(std::uint32_t word) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << word;
    return oss.str();
}

std::string format_command26_hex(std::uint32_t command) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(7) << std::setfill('0')
        << command;
    return oss.str();
}

}  // namespace hpu
