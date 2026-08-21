#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "instruction.hpp"

namespace hpu {

EncodedInstruction assemble_line(std::string_view line);
std::vector<EncodedInstruction> assemble_source(std::string_view source);
std::string format_word_hex(std::uint32_t word);
std::string format_command26_hex(std::uint32_t command);

}  // namespace hpu
