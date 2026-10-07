#include "fixture.hpp"
#include "poseidon_bridge.hpp"

namespace {
RawCiphertext raw(const seal::Ciphertext &source) {
    RawCiphertext result{source.poly_modulus_degree(), source.coeff_modulus_size(),
        source.size(), source.is_ntt_form(), source.scale(), source.correction_factor(), {}};
    const auto count = result.degree * result.limbs * result.components;
    if (count) result.words.assign(source.data(), source.data() + count);
    return result;
}
template<class Keys> RawKeys raw_keys(const Keys &source) {
    RawKeys result(source.data().size());
    for (std::size_t i = 0; i < result.size(); ++i)
        for (const auto &key : source.data()[i]) result[i].push_back(raw(key.data()));
    return result;
}
} // namespace

PoseidonOracle verify_poseidon(const Fixture &f) {
    PoseidonInputs inputs;
    inputs.scheme = f.scheme;
    inputs.operation = f.operation;
    inputs.degree = f.degree;
    inputs.rotation_steps = f.rotation_steps;
    const auto key_level = f.context->key_context_data();
    const auto &moduli = key_level->parms().coeff_modulus();
    inputs.plain_modulus = key_level->parms().plain_modulus().value();
    for (std::size_t i = 0; i < moduli.size(); ++i) {
        (i + 1 == moduli.size() ? inputs.p : inputs.q).push_back(moduli[i].value());
        inputs.ntt_roots.push_back(key_level->small_ntt_tables()[i].get_root());
    }
    inputs.left = raw(f.left); inputs.right = raw(f.right); inputs.input = raw(f.input);
    inputs.tensor = raw(f.tensor); inputs.expected = raw(f.expected);
    inputs.relin = raw_keys(f.relin); inputs.galois = raw_keys(f.galois);
    if (f.scheme == "bfv" && f.operation == "hmul") {
        const auto &key = f.keys->secret_key().data();
        inputs.secret_key.assign(key.data(), key.data() + key.coeff_count());
        seal::Decryptor decryptor(*f.context, f.keys->secret_key());
        seal::Plaintext plain;
        decryptor.decrypt(f.expected, plain);
        inputs.expected_plain.assign(plain.data(), plain.data() + plain.coeff_count());
        inputs.expected_plain.resize(f.degree, 0);
    }
    return verify_poseidon_raw(inputs);
}
