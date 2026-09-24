#include "hpu/runtime/memory_image.hpp"
#include "hpu/seal/application_image.hpp"
#include "hpu/seal/ckks_context.hpp"
#include "hpu/seal/ckks_delivery.hpp"
#include "hpu/seal/ntt_bridge.hpp"
#include "hpu/seal/operation_codegen.hpp"
#include "hpu/seal/operation_plan.hpp"
#include "hpu/seal/operation_relocation.hpp"
#include "hpu/seal/operation_runtime.hpp"
#include "hpu/seal/software_executor.hpp"

#include <seal/seal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kStem = "ckks_reline";
constexpr const char* kApi =
    "hpu::seal_adapter::CkksOperationPlan::append_relinearize";
constexpr std::size_t kDegree = 4096;
constexpr std::size_t kExpectedQ = 4;
constexpr std::size_t kInputComponents = 3;
constexpr std::size_t kOutputComponents = 2;
constexpr std::uint64_t kGuardLines = 64;
constexpr std::uint64_t kCapacityLines = 65536;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream output(path, std::ios::binary);
    if (!output || !(output << contents)) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

std::size_t count_token(const std::string& text, const std::string& token)
{
    std::size_t count = 0;
    for (std::size_t position = 0;
         (position = text.find(token, position)) != std::string::npos;
         position += token.size()) {
        ++count;
    }
    return count;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 3) {
            throw std::invalid_argument(
                "usage: hpu_seal_cmb012_generator OUTPUT_DIR PRODUCER_COMMIT");
        }
        const std::filesystem::path output_directory(argv[1]);
        const std::string producer_commit(argv[2]);
        require(producer_commit.size() == 40
                    && std::all_of(
                        producer_commit.begin(), producer_commit.end(),
                        [](char character) {
                            return (character >= '0' && character <= '9')
                                || (character >= 'a' && character <= 'f');
                        }),
                "producer commit must be a full lowercase SHA-1");

        hpu::seal_adapter::CkksContextSpec spec;
        spec.poly_modulus_degree = kDegree;
        spec.coeff_modulus_bits = {27, 27, 27, 27, 27};
        const auto bundle = hpu::seal_adapter::create_ckks_context(spec);
        const hpu::seal_adapter::CkksLevelChain level_chain(*bundle.context);
        const auto& top = level_chain.top();
        require(top.q_moduli.size() == kExpectedQ
                    && top.special_moduli.size() == 1,
                "CKKS context differs from the fixed CMB012 Q4|P1 contract");

        ::seal::KeyGenerator key_generator(*bundle.context);
        ::seal::PublicKey public_key;
        ::seal::RelinKeys relinearization_keys;
        key_generator.create_public_key(public_key);
        key_generator.create_relin_keys(relinearization_keys);

        ::seal::CKKSEncoder encoder(*bundle.context);
        const std::vector<double> left_values{0.25, -1.5, 2.0, -0.75};
        const std::vector<double> right_values{4.0, 0.5, -1.25, -2.0};
        constexpr double scale = 1048576.0; // 2^20
        ::seal::Plaintext left_plaintext;
        ::seal::Plaintext right_plaintext;
        encoder.encode(left_values, scale, left_plaintext);
        encoder.encode(right_values, scale, right_plaintext);
        ::seal::Encryptor encryptor(*bundle.context, public_key);
        ::seal::Ciphertext left;
        ::seal::Ciphertext right;
        encryptor.encrypt(left_plaintext, left);
        encryptor.encrypt(right_plaintext, right);

        ::seal::Evaluator evaluator(*bundle.context);
        ::seal::Ciphertext tensor;
        evaluator.multiply(left, right, tensor);
        require(tensor.size() == kInputComponents && tensor.is_ntt_form(),
                "SEAL multiply did not produce a three-component NTT tensor");
        ::seal::Ciphertext expected = tensor;
        evaluator.relinearize_inplace(expected, relinearization_keys);
        require(expected.size() == kOutputComponents && expected.is_ntt_form(),
                "SEAL Reline did not produce a two-component NTT ciphertext");

        ::seal::Decryptor decryptor(*bundle.context, key_generator.secret_key());
        ::seal::Plaintext decrypted;
        decryptor.decrypt(expected, decrypted);
        std::vector<double> decoded;
        encoder.decode(decrypted, decoded);
        double maximum_error = 0.0;
        for (std::size_t index = 0; index < left_values.size(); ++index) {
            maximum_error = std::max(
                maximum_error,
                std::abs(decoded[index] - left_values[index] * right_values[index]));
        }
        require(maximum_error <= 5e-3,
                "SEAL CMB012 semantic oracle exceeded tolerance: "
                    + std::to_string(maximum_error));

        hpu::seal_adapter::CkksApplicationImageBuilder image_builder(
            *bundle.context, kCapacityLines);
        image_builder.add_modulus_table();
        const auto canonical_twiddles = image_builder.add_canonical_twiddles();
        // Relinearize performs the input NTT->coefficient conversion in place.
        // Reserve a writable three-component input instead of presenting a
        // read-only ciphertext allocation, then seed it below with the exact
        // SEAL tensor words.
        const auto prepared_tensor = image_builder.reserve_ciphertext(
            "input/tensor", top, kInputComponents, tensor.scale());
        const auto relinearization_key = image_builder.add_relinearization_key(
            "key/relinearization/top", relinearization_keys, top);
        const auto keyswitch_constants = image_builder.add_keyswitch_constants(
            "constants/keyswitch/top", top);

        hpu::seal_adapter::CkksOperationPlan plan(image_builder);
        const auto output = plan.append_relinearize(
            "relinearize", prepared_tensor, relinearization_key,
            keyswitch_constants, "output/relinearized");
        require(plan.steps().size() == 1
                    && plan.steps().front().kind
                        == hpu::seal_adapter::CkksOperationKind::relinearize
                    && plan.steps().front().inputs.size() == 1
                    && plan.steps().front().inputs.front().component_count
                        == kInputComponents
                    && output.components.size() == kOutputComponents
                    && output.domain
                        == hpu::runtime::PolynomialDomain::canonical_ntt_physical,
                "CMB012 plan is not one standalone CKKS Reline operation");

        // The builder intentionally exposes the completed image read-only.
        // Its underlying object is non-const here; seed only the spans just
        // reserved for this writable external input before any executor copy.
        auto& initial_image = const_cast<std::vector<std::uint32_t>&>(
            image_builder.image().words());
        for (std::size_t component = 0; component < kInputComponents; ++component) {
            const auto polynomial = hpu::seal_adapter::ciphertext_component_to_hpu(
                tensor, component, *bundle.context);
            require(polynomial.modulus_ids == prepared_tensor.components[component].modulus_ids,
                    "CMB012 tensor modulus order differs from the reserved input");
            for (std::size_t basis = 0; basis < polynomial.modulus_ids.size(); ++basis) {
                const auto first = polynomial.words.begin()
                    + static_cast<std::ptrdiff_t>(basis * kDegree);
                const auto span = prepared_tensor.components[component].limbs[basis];
                require(span.line_count * hpu::runtime::kHpuMemLineWords == kDegree,
                        "CMB012 tensor limb is not one exact N4096 allocation");
                const auto destination = initial_image.begin()
                    + static_cast<std::ptrdiff_t>(
                        span.line_offset * hpu::runtime::kHpuMemLineWords);
                std::copy(first, first + kDegree, destination);
            }
        }

        hpu::seal_adapter::CkksSoftwareExecutor software_executor(
            *bundle.context, image_builder.image());
        software_executor.relinearize(
            prepared_tensor, relinearization_key, keyswitch_constants,
            output, canonical_twiddles);
        for (std::size_t component = 0; component < kOutputComponents; ++component) {
            const auto seal_words = hpu::seal_adapter::hpu_to_seal_ntt(
                software_executor.export_component(output, component),
                output.parms_id, *bundle.context);
            require(std::equal(
                        seal_words.begin(), seal_words.end(),
                        expected.data(component)),
                    "HPU_SEAL Reline software result differs from SEAL");
        }

        const auto lowered = hpu::seal_adapter::lower_ckks_operation_plan(
            plan, *bundle.context);
        require(lowered.operations.size() == 1
                    && count_token(lowered.body_asm, "psync") == 1,
                "CMB012 lowering is not one standalone terminal program");
        const auto relocation = hpu::seal_adapter::build_ckks_relocation_schedule(
            lowered, image_builder.image(), *bundle.context);
        require(relocation.complete()
                    && relocation.bindings.size() == relocation.expected_dma_count,
                "CMB012 Reline DMA relocation is incomplete");
        const auto runtime = hpu::seal_adapter::lower_ckks_runtime_program(
            lowered, relocation);
        require(image_builder.image().used_lines() + kGuardLines
                    <= image_builder.image().capacity_lines(),
                "CMB012 image leaves no room for the AM guard");
        const auto artifacts = hpu::seal_adapter::render_ckks_runtime_artifacts(
            kStem, runtime, image_builder.image().capacity_lines());
        const auto& expected_image = software_executor.memory().words();
        hpu::seal_adapter::write_ckks_delivery_package(
            output_directory, kStem, lowered, runtime, artifacts,
            image_builder.image(), &expected_image);

        std::ostringstream metadata;
        metadata << "{\n"
                 << "  \"format_version\": 1,\n"
                 << "  \"case_id\": \"HPU_IT_DIR_CMB_012\",\n"
                 << "  \"scheme\": \"CKKS\",\n"
                 << "  \"api\": \"" << kApi << "\",\n"
                 << "  \"producer_commit\": \"" << producer_commit << "\",\n"
                 << "  \"poly_modulus_degree\": " << kDegree << ",\n"
                 << "  \"q_count\": " << top.q_moduli.size() << ",\n"
                 << "  \"special_modulus_count\": "
                 << top.special_moduli.size() << ",\n"
                 << "  \"input_component_count\": " << kInputComponents << ",\n"
                 << "  \"output_component_count\": " << kOutputComponents << ",\n"
                 << "  \"domain\": \"canonical_ntt_physical\",\n"
                 << "  \"instruction_count\": " << runtime.instructions.size() << ",\n"
                 << "  \"dma_count\": " << runtime.dma.size() << ",\n"
                 << "  \"image_used_lines\": "
                 << image_builder.image().used_lines() << ",\n"
                 << "  \"guard_lines\": " << kGuardLines << "\n"
                 << "}\n";
        write_text(output_directory / "CMB012_METADATA.json", metadata.str());

        std::cout << std::setprecision(8)
                  << "HPU_SEAL CKKS Reline: N=" << kDegree
                  << " Q=" << top.q_moduli.size()
                  << " input_components=" << tensor.size()
                  << " output_components=" << expected.size()
                  << " instructions=" << runtime.instructions.size()
                  << " dma=" << runtime.dma.size()
                  << " used_lines=" << image_builder.image().used_lines()
                  << " semantic_error=" << maximum_error << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HPU_SEAL CMB012 generation failed: " << error.what() << '\n';
        return 1;
    }
}
