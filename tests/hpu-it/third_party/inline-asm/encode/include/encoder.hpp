#pragma once

#include <cstdint>

#include "instruction.hpp"

namespace hpu {

std::uint32_t encode_instruction(const Instruction& instruction);
std::uint32_t precode_command26(std::uint32_t instruction_word);

}  // namespace hpu
