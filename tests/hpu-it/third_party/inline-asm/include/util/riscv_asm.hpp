#pragma once

#include <string>
#include <sstream>

namespace riscv {

inline std::string xreg(int id) {
    return "x" + std::to_string(id);
}

inline std::string emit_raw(const std::string& inst) {
    std::ostringstream ss;
    ss << "        \"" << inst << " \\n\\t\"\n";
    return ss.str();
}

inline std::string label(const std::string& name) {
    std::ostringstream ss;
    ss << "        \"" << name << ": \\n\\t\"\n";
    return ss.str();
}

inline std::string nop() { return emit_raw("nop"); }
inline std::string ret() { return emit_raw("ret"); }

inline std::string li(const std::string& rd, long imm) {
    std::ostringstream ss;
    ss << "        \"li " << rd << ", " << imm << " \\n\\t\"\n";
    return ss.str();
}

inline std::string la(const std::string& rd, const std::string& symbol) {
    std::ostringstream ss;
    ss << "        \"la " << rd << ", " << symbol << " \\n\\t\"\n";
    return ss.str();
}

inline std::string mv(const std::string& rd, const std::string& rs) {
    std::ostringstream ss;
    ss << "        \"mv " << rd << ", " << rs << " \\n\\t\"\n";
    return ss.str();
}

inline std::string add(const std::string& rd, const std::string& rs1, const std::string& rs2) {
    std::ostringstream ss;
    ss << "        \"add " << rd << ", " << rs1 << ", " << rs2 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string sub(const std::string& rd, const std::string& rs1, const std::string& rs2) {
    std::ostringstream ss;
    ss << "        \"sub " << rd << ", " << rs1 << ", " << rs2 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string mul(const std::string& rd, const std::string& rs1, const std::string& rs2) {
    std::ostringstream ss;
    ss << "        \"mul " << rd << ", " << rs1 << ", " << rs2 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string div(const std::string& rd, const std::string& rs1, const std::string& rs2) {
    std::ostringstream ss;
    ss << "        \"div " << rd << ", " << rs1 << ", " << rs2 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string rem(const std::string& rd, const std::string& rs1, const std::string& rs2) {
    std::ostringstream ss;
    ss << "        \"rem " << rd << ", " << rs1 << ", " << rs2 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string addi(const std::string& rd, const std::string& rs1, int imm12) {
    std::ostringstream ss;
    ss << "        \"addi " << rd << ", " << rs1 << ", " << imm12 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string andi(const std::string& rd, const std::string& rs1, int imm12) {
    std::ostringstream ss;
    ss << "        \"andi " << rd << ", " << rs1 << ", " << imm12 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string ori(const std::string& rd, const std::string& rs1, int imm12) {
    std::ostringstream ss;
    ss << "        \"ori " << rd << ", " << rs1 << ", " << imm12 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string xori(const std::string& rd, const std::string& rs1, int imm12) {
    std::ostringstream ss;
    ss << "        \"xori " << rd << ", " << rs1 << ", " << imm12 << " \\n\\t\"\n";
    return ss.str();
}

inline std::string slli(const std::string& rd, const std::string& rs1, int shamt) {
    std::ostringstream ss;
    ss << "        \"slli " << rd << ", " << rs1 << ", " << shamt << " \\n\\t\"\n";
    return ss.str();
}

inline std::string srli(const std::string& rd, const std::string& rs1, int shamt) {
    std::ostringstream ss;
    ss << "        \"srli " << rd << ", " << rs1 << ", " << shamt << " \\n\\t\"\n";
    return ss.str();
}

inline std::string srai(const std::string& rd, const std::string& rs1, int shamt) {
    std::ostringstream ss;
    ss << "        \"srai " << rd << ", " << rs1 << ", " << shamt << " \\n\\t\"\n";
    return ss.str();
}

inline std::string lw(const std::string& rd, int offset, const std::string& rs1) {
    std::ostringstream ss;
    ss << "        \"lw " << rd << ", " << offset << "(" << rs1 << ") \\n\\t\"\n";
    return ss.str();
}

inline std::string sw(const std::string& rs2, int offset, const std::string& rs1) {
    std::ostringstream ss;
    ss << "        \"sw " << rs2 << ", " << offset << "(" << rs1 << ") \\n\\t\"\n";
    return ss.str();
}

inline std::string ld(const std::string& rd, int offset, const std::string& rs1) {
    std::ostringstream ss;
    ss << "        \"ld " << rd << ", " << offset << "(" << rs1 << ") \\n\\t\"\n";
    return ss.str();
}

inline std::string sd(const std::string& rs2, int offset, const std::string& rs1) {
    std::ostringstream ss;
    ss << "        \"sd " << rs2 << ", " << offset << "(" << rs1 << ") \\n\\t\"\n";
    return ss.str();
}

inline std::string beq(const std::string& rs1, const std::string& rs2, const std::string& target) {
    std::ostringstream ss;
    ss << "        \"beq " << rs1 << ", " << rs2 << ", " << target << " \\n\\t\"\n";
    return ss.str();
}

inline std::string bne(const std::string& rs1, const std::string& rs2, const std::string& target) {
    std::ostringstream ss;
    ss << "        \"bne " << rs1 << ", " << rs2 << ", " << target << " \\n\\t\"\n";
    return ss.str();
}

inline std::string blt(const std::string& rs1, const std::string& rs2, const std::string& target) {
    std::ostringstream ss;
    ss << "        \"blt " << rs1 << ", " << rs2 << ", " << target << " \\n\\t\"\n";
    return ss.str();
}

inline std::string bge(const std::string& rs1, const std::string& rs2, const std::string& target) {
    std::ostringstream ss;
    ss << "        \"bge " << rs1 << ", " << rs2 << ", " << target << " \\n\\t\"\n";
    return ss.str();
}

inline std::string j(const std::string& target) {
    std::ostringstream ss;
    ss << "        \"j " << target << " \\n\\t\"\n";
    return ss.str();
}

inline std::string jal(const std::string& rd, const std::string& target) {
    std::ostringstream ss;
    ss << "        \"jal " << rd << ", " << target << " \\n\\t\"\n";
    return ss.str();
}

inline std::string jalr(const std::string& rd, const std::string& rs1, int imm12 = 0) {
    std::ostringstream ss;
    ss << "        \"jalr " << rd << ", " << imm12 << "(" << rs1 << ") \\n\\t\"\n";
    return ss.str();
}

} // namespace riscv
