#pragma once
#include <seal/seal.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Fixture {
    std::shared_ptr<seal::SEALContext> context;
    std::unique_ptr<seal::KeyGenerator> keys;
    seal::RelinKeys relin;
    seal::GaloisKeys galois;
    seal::Ciphertext left, right, input, tensor, expected;
    std::string scheme, operation, stem;
    std::size_t degree;
    std::filesystem::path directory;

    Fixture(std::string s, std::string op, std::size_t n, std::filesystem::path out, int scale_bits=30)
        : scheme(s), operation(op), degree(n), directory(std::move(out)) {
        if (n < 128 || n > 65536 || (n & (n - 1)))
            throw std::invalid_argument("degree must be a power of two in [128,65536]");
        const auto type = scheme == "ckks" ? seal::scheme_type::ckks :
                          scheme == "bfv" ? seal::scheme_type::bfv : seal::scheme_type::bgv;
        seal::EncryptionParameters parameters(type);
        parameters.set_poly_modulus_degree(n);
        if (scheme == "bgv")
            parameters.set_coeff_modulus({seal::Modulus(2013265921U), seal::Modulus(1811939329U),
                seal::Modulus(469762049U), seal::Modulus(1224736769U)});
        else parameters.set_coeff_modulus(seal::CoeffModulus::Create(n, {27,27,27,27,27}));
        if (scheme != "ckks") parameters.set_plain_modulus(n > 32768 ? 786433 : 65537);
        // 每个规模有固定随机流；同一批 main/SEAL 输入、密钥与 oracle 可复现。
        seal::prng_seed_type seed{0x485055534348454dULL, n, 1, 2, 3, 4, 5, 6};
        parameters.set_random_generator(std::make_shared<seal::Blake2xbPRNGFactory>(seed));
        context = std::make_shared<seal::SEALContext>(parameters, true, seal::sec_level_type::none);
        if (!context->parameters_set()) throw std::runtime_error(context->parameter_error_message());
        keys = std::make_unique<seal::KeyGenerator>(*context);
        if (op == "keyswitch" || op == "reline" || op == "hmul") keys->create_relin_keys(relin);
        if (op == "rotate") keys->create_galois_keys(std::vector<int>{1}, galois);
        seal::Plaintext a, b;
        if (scheme == "ckks") {
            seal::CKKSEncoder encoder(*context);
            const double scale = op == "modswitch" ? std::ldexp(1.0, 50) : std::ldexp(1.0, scale_bits);
            encoder.encode(std::vector<double>{0.25,-1.5,2.0,0.75}, scale, a);
            encoder.encode(std::vector<double>{2.0,0.5,-1.25,1.5}, scale, b);
        } else {
            seal::BatchEncoder encoder(*context);
            std::vector<std::uint64_t> av(n), bv(n);
            for (std::size_t i = 0; i < n; ++i) { av[i] = i % 5 + 1; bv[i] = i % 3 + 1; }
            encoder.encode(av, a); encoder.encode(bv, b);
        }
        seal::Encryptor encryptor(*context, keys->secret_key());
        encryptor.encrypt_symmetric(a, left); encryptor.encrypt_symmetric(b, right);
        if (scheme == "bgv") {
            // 非平凡 cf，避免把 BGV 误测成 CKKS 的逐点算术。
            const auto t = parameters.plain_modulus().value();
            for (auto *ciphertext : {&left, &right}) {
                const std::uint64_t factor = ciphertext == &left ? 3 : 5;
                const auto &moduli = context->first_context_data()->parms().coeff_modulus();
                for (std::size_t c = 0; c < ciphertext->size(); ++c)
                    for (std::size_t q = 0; q < moduli.size(); ++q)
                        for (std::size_t j = 0; j < n; ++j)
                            ciphertext->data(c)[q*n+j] = ciphertext->data(c)[q*n+j] * factor % moduli[q].value();
                ciphertext->correction_factor() = factor % t;
            }
        }
        seal::Evaluator evaluator(*context);
        input = left;
        if (op == "keyswitch" || op == "reline") {
            evaluator.multiply(left, right, input);
            if (op == "keyswitch") {
                // 仅保留待切换的 c2，区分 KeySwitch 核心与完整 c0/c1 合并的 Reline。
                const auto words = n * input.coeff_modulus_size();
                std::fill(input.data(0), input.data(0) + 2 * words, 0);
            }
            evaluator.relinearize(input, relin, expected);
        } else if (op == "hadd") evaluator.add(left, right, expected);
        else if (op == "hmul") {
            evaluator.multiply(left, right, tensor);
            evaluator.relinearize(tensor, relin, expected);
        }
        else if (op == "modswitch") {
            if (scheme == "ckks") evaluator.rescale_to_next(input, expected);
            else evaluator.mod_switch_to_next(input, expected);
        } else if (op == "rotate") {
            if (scheme == "ckks") evaluator.rotate_vector(input, 1, galois, expected);
            else evaluator.rotate_rows(input, 1, galois, expected);
        } else throw std::invalid_argument("unknown operation");
        stem = scheme + "_" + op + "_n" + std::to_string(n);
        if (scale_bits != 30) stem += "_scale" + std::to_string(scale_bits);
    }
};

void generate_ckks(Fixture &);
void generate_bfv(Fixture &);
void generate_bgv(Fixture &);
