#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 两库都给std::array<uint64_t,4>定义了std::hash，不能在同一翻译单元
// 包含它们的头文件。边界只传普通数据，不依赖任一库的内部类型。
struct RawCiphertext {
    std::size_t degree{}, limbs{}, components{};
    bool ntt{};
    double scale{};
    std::uint64_t correction{};
    std::vector<std::uint64_t> words;
};
using RawKeys = std::vector<std::vector<RawCiphertext>>;
struct PoseidonInputs {
    std::string scheme, operation;
    std::size_t degree{};
    int rotation_steps{};
    std::uint64_t plain_modulus{};
    std::vector<std::uint64_t> q, p, ntt_roots;
    RawCiphertext left, right, input, tensor, expected;
    RawKeys relin, galois;
    // 仅在主机进程内用于BFV乘法语义对拍；绝不写入目标数据包。
    std::vector<std::uint64_t> secret_key, expected_plain;
};

struct PoseidonOracle {
    std::string api;
    std::size_t raw_word_mismatches{}, plaintext_coefficients_compared{};
};
PoseidonOracle verify_poseidon_raw(const PoseidonInputs &inputs);

// 用同一份密文、上下文和评估密钥调用真实Poseidon API；不重新随机加密。
// 返回实际API和比较证据；BFV乘法按明文语义，其余按原始word验收。
struct Fixture;
PoseidonOracle verify_poseidon(const Fixture &fixture);
