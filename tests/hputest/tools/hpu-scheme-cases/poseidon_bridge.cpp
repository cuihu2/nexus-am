#include "poseidon_bridge.hpp"
#include <poseidon/factory/poseidon_factory.h>
#include <poseidon/key/galoiskeys.h>
#include <poseidon/key/relinkeys.h>
#include <poseidon/decryptor.h>
#include <algorithm>
#include <sstream>

namespace {
poseidon::ParametersLiteral parameters(const PoseidonInputs &f) {
    std::vector<poseidon::Modulus> q, p;
    for (auto value : f.q) q.emplace_back(value);
    for (auto value : f.p) p.emplace_back(value);
    const auto scheme = f.scheme == "ckks" ? CKKS : f.scheme == "bfv" ? BFV : BGV;
    unsigned log_n = 0;
    for (auto n = f.degree; n > 1; n >>= 1) ++log_n;
    // N128是功能验证参数，明确关闭安全等级校验；不是部署安全参数。
    return {scheme, log_n, log_n - (f.scheme == "ckks" ? 1U : 0U), 30, 64, 0,
            poseidon::Modulus(f.plain_modulus), q, p,
            poseidon::sec_level_type::none};
}

poseidon::parms_id_type level_id(const poseidon::PoseidonContext &context, std::size_t limbs) {
    for (auto level = context.crt_context()->key_context_data(); level;
         level = level->next_context_data()) {
        if (level->coeff_modulus().size() == limbs) return level->parms().parms_id();
    }
    throw std::runtime_error("Poseidon has no matching Q/P level");
}

poseidon::Ciphertext import_ciphertext(const RawCiphertext &source,
                                     const poseidon::PoseidonContext &context) {
    poseidon::Ciphertext result;
    result.resize(context, level_id(context, source.limbs), source.components);
    result.is_ntt_form() = source.ntt;
    result.scale() = source.scale;
    result.correction_factor() = source.correction;
    std::copy(source.words.begin(), source.words.end(), result.data());
    return result;
}

template<class Destination>
void import_keys(const RawKeys &source, Destination &destination,
                 const poseidon::PoseidonContext &context) {
    destination.parms_id() = context.crt_context()->key_context_data()->parms().parms_id();
    destination.data().resize(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        destination.data()[i].resize(source[i].size());
        for (std::size_t j = 0; j < source[i].size(); ++j)
            destination.data()[i][j].data() = import_ciphertext(source[i][j], context);
    }
}

void compare(const poseidon::Ciphertext &actual, const RawCiphertext &expected,
             const std::string &label, const poseidon::PoseidonContext &context) {
    if (actual.size() != expected.components ||
        actual.poly_modulus_degree() != expected.degree ||
        actual.coeff_modulus_size() != expected.limbs ||
        actual.parms_id() != level_id(context, expected.limbs) ||
        actual.is_ntt_form() != expected.ntt ||
        actual.correction_factor() != expected.correction ||
        actual.scale() != expected.scale)
        throw std::runtime_error(label + ": Poseidon/SEAL ciphertext metadata differs");
    const auto words = expected.words.size();
    for (std::size_t i = 0; i < words; ++i) {
        if (actual.data()[i] != expected.words[i]) {
            std::ostringstream message;
            message << label << ": Poseidon/SEAL raw-word mismatch at " << i
                    << " actual=" << actual.data()[i] << " expected=" << expected.words[i];
            throw std::runtime_error(message.str());
        }
    }
}

void compare_bfv_plain(const poseidon::Ciphertext &actual, const PoseidonInputs &f,
                       const poseidon::PoseidonContext &context) {
    poseidon::SecretKey key;
    key.data().resize(f.secret_key.size());
    key.data().parms_id() = context.crt_context()->key_context_data()->parms().parms_id();
    std::copy(f.secret_key.begin(), f.secret_key.end(), key.data().data());
    poseidon::Decryptor decryptor(context, key);
    poseidon::Plaintext plain;
    decryptor.decrypt(actual, plain);
    // 两种BEHZ实现允许不同噪声密文；接口要求明文模t严格相等，不用误差容限。
    for (std::size_t i = 0; i < f.degree; ++i) {
        const auto value = i < plain.coeff_count() ? plain.data()[i] : 0;
        if (value != f.expected_plain.at(i))
            throw std::runtime_error("Poseidon BFV multiply decrypt mismatch at coefficient " +
                                     std::to_string(i));
    }
}

std::size_t raw_mismatches(const poseidon::Ciphertext &actual, const RawCiphertext &expected) {
    if (actual.size() * actual.poly_modulus_degree() * actual.coeff_modulus_size() != expected.words.size())
        throw std::runtime_error("BFV raw comparison shape differs");
    std::size_t count = 0;
    for (std::size_t i = 0; i < expected.words.size(); ++i)
        count += actual.data()[i] != expected.words[i];
    return count;
}

template<class Evaluator>
void arithmetic(Evaluator &evaluator, const PoseidonInputs &f, const poseidon::Ciphertext &left,
                const poseidon::Ciphertext &right, const poseidon::Ciphertext &input,
                const poseidon::RelinKeys &keys, poseidon::Ciphertext &result,
                const poseidon::PoseidonContext &context, PoseidonOracle &oracle) {
    if (f.operation == "hadd") evaluator.add(left, right, result);
    else if (f.operation == "reline") evaluator.relinearize(input, result, keys);
    else if (f.operation == "hmul") {
        poseidon::Ciphertext tensor;
        evaluator.multiply(left, right, tensor);
        if (f.scheme == "bfv") {
            compare_bfv_plain(tensor, f, context);
            oracle.raw_word_mismatches += raw_mismatches(tensor, f.tensor);
            oracle.plaintext_coefficients_compared += f.degree;
        }
        else compare(tensor, f.tensor, "multiply/tensor", context);
        evaluator.multiply_relin(left, right, result, keys);
    }
}
} // namespace

PoseidonOracle verify_poseidon_raw(const PoseidonInputs &f) {
    PoseidonOracle oracle;
    auto *factory = poseidon::PoseidonFactory::get_instance();
    factory->set_device_type(poseidon::DEVICE_SOFTWARE);
    auto context = factory->create_poseidon_context(parameters(f));
    if (context.key_switch_variant() != BV)
        throw std::runtime_error("Poseidon bridge requires BV, Q>=2 and P=1");
    // 拷贝NTT数组前先验证根和模数排列，不能靠同长度推断格式相同。
    for (std::size_t i = 0; i < f.ntt_roots.size(); ++i) {
        if (f.ntt_roots[i] !=
            context.crt_context()->small_ntt_tables()[i].get_root())
            throw std::runtime_error("Poseidon/SEAL NTT roots differ");
    }
    const auto left = import_ciphertext(f.left, context);
    const auto right = import_ciphertext(f.right, context);
    const auto input = import_ciphertext(f.input, context);
    poseidon::RelinKeys relin;
    poseidon::GaloisKeys galois;
    import_keys(f.relin, relin, context);
    import_keys(f.galois, galois, context);
    poseidon::Ciphertext result;
    std::string api;
    if (f.scheme == "ckks") {
        auto evaluator = factory->create_ckks_evaluator(context);
        arithmetic(*evaluator, f, left, right, input, relin, result, context, oracle);
        if (f.operation == "modswitch") evaluator->rescale(input, result);
        if (f.operation == "rotate") evaluator->rotate(input, result, f.rotation_steps, galois);
        api = "EvaluatorCkksBase::";
    } else if (f.scheme == "bfv") {
        auto evaluator = factory->create_bfv_evaluator(context);
        arithmetic(*evaluator, f, left, right, input, relin, result, context, oracle);
        if (f.operation == "modswitch") evaluator->drop_modulus_to_next(input, result);
        if (f.operation == "rotate") evaluator->rotate_row(input, result, f.rotation_steps, galois);
        api = "EvaluatorBfvBase::";
    } else {
        auto evaluator = factory->create_bgv_evaluator(context);
        arithmetic(*evaluator, f, left, right, input, relin, result, context, oracle);
        if (f.operation == "modswitch") evaluator->drop_modulus_to_next(input, result);
        if (f.operation == "rotate") evaluator->rotate_row(input, result, f.rotation_steps, galois);
        api = "EvaluatorBgvBase::";
    }
    if (f.scheme == "bfv" && f.operation == "hmul") {
        // 尺寸、domain和level仍必须一致；不同BEHZ实现不要求噪声密文逐字相同。
        if (result.size() != 2 || result.is_ntt_form() || result.coeff_modulus_size() != f.q.size() ||
            result.poly_modulus_degree() != f.degree ||
            result.parms_id() != level_id(context, f.expected.limbs) ||
            result.scale() != f.expected.scale || result.correction_factor() != f.expected.correction)
            throw std::runtime_error("Poseidon BFV multiply output shape differs");
        compare_bfv_plain(result, f, context);
        oracle.raw_word_mismatches += raw_mismatches(result, f.expected);
        oracle.plaintext_coefficients_compared += f.degree;
    } else compare(result, f.expected, f.scheme + "/" + f.operation, context);
    if (f.operation == "hadd") api += "add";
    else if (f.operation == "hmul") api += "multiply_relin";
    else if (f.operation == "reline") api += "relinearize";
    else if (f.operation == "rotate") api += f.scheme == "ckks" ? "rotate" : "rotate_row";
    else api += f.scheme == "ckks" ? "rescale" : "drop_modulus_to_next";
    oracle.api = api;
    return oracle;
}
