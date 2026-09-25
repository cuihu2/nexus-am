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
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kStem = "ckks_rescale";
constexpr const char* kApi =
    "hpu::seal_adapter::CkksOperationPlan::append_rescale";
constexpr std::size_t kDegree = 4096;
constexpr std::size_t kInputQ = 4;
constexpr std::size_t kOutputQ = 3;
constexpr std::size_t kComponents = 2;
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
                "usage: hpu_seal_cmb013_generator OUTPUT_DIR PRODUCER_COMMIT");
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
        const auto& next = level_chain.next(top.parms_id);
        require(top.q_moduli.size() == kInputQ
                    && next.q_moduli.size() == kOutputQ
                    && top.special_moduli.size() == 1,
                "CKKS context differs from the fixed CMB013 Q4-to-Q3 contract");

        ::seal::KeyGenerator key_generator(*bundle.context);
        ::seal::PublicKey public_key;
        ::seal::RelinKeys relinearization_keys;
        key_generator.create_public_key(public_key);
        key_generator.create_relin_keys(relinearization_keys);

        ::seal::CKKSEncoder encoder(*bundle.context);
        const std::vector<double> left_values{0.25, -1.5, 2.0, -0.75};
        const std::vector<double> right_values{4.0, 0.5, -1.25, -2.0};
        constexpr double input_operand_scale = 33554432.0; // 2^25
        ::seal::Plaintext left_plaintext;
        ::seal::Plaintext right_plaintext;
        encoder.encode(left_values, input_operand_scale, left_plaintext);
        encoder.encode(right_values, input_operand_scale, right_plaintext);
        ::seal::Encryptor encryptor(*bundle.context, public_key);
        ::seal::Ciphertext left;
        ::seal::Ciphertext right;
        encryptor.encrypt(left_plaintext, left);
        encryptor.encrypt(right_plaintext, right);

        // Form a valid Q4, two-component CKKS value on the host.  The emitted
        // HPU program starts from this value and contains Rescale only.
        ::seal::Evaluator evaluator(*bundle.context);
        ::seal::Ciphertext input;
        evaluator.multiply(left, right, input);
        evaluator.relinearize_inplace(input, relinearization_keys);
        require(input.size() == kComponents && input.is_ntt_form()
                    && input.parms_id() == top.parms_id,
                "SEAL did not produce the fixed Q4 Rescale input");
        const double input_scale = input.scale();
        ::seal::Ciphertext expected = input;
        evaluator.rescale_to_next_inplace(expected);
        require(expected.size() == kComponents && expected.is_ntt_form()
                    && expected.parms_id() == next.parms_id,
                "SEAL Rescale did not produce a two-component Q3 ciphertext");

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
                "SEAL CMB013 semantic oracle exceeded tolerance: "
                    + std::to_string(maximum_error));

        hpu::seal_adapter::CkksApplicationImageBuilder image_builder(
            *bundle.context, kCapacityLines);
        image_builder.add_modulus_table();
        const auto canonical_twiddles = image_builder.add_canonical_twiddles();
        // Rescale performs the input NTT->coefficient conversion in place.
        // Reserve a writable input and seed it with the exact SEAL words.
        const auto prepared_input = image_builder.reserve_ciphertext(
            "input/ciphertext", top, kComponents, input.scale());
        const auto rescale_constants = image_builder.add_rescale_constants(
            "constants/rescale/top_to_next", top);

        hpu::seal_adapter::CkksOperationPlan plan(image_builder);
        const auto output = plan.append_rescale(
            "rescale", prepared_input, rescale_constants, "output/rescaled");
        require(plan.steps().size() == 1
                    && plan.steps().front().kind
                        == hpu::seal_adapter::CkksOperationKind::rescale
                    && plan.steps().front().inputs.size() == 1
                    && plan.steps().front().inputs.front().component_count
                        == kComponents
                    && output.components.size() == kComponents
                    && output.parms_id == next.parms_id
                    && output.domain
                        == hpu::runtime::PolynomialDomain::canonical_ntt_physical,
                "CMB013 plan is not one standalone CKKS Rescale operation");

        auto& initial_image = const_cast<std::vector<std::uint32_t>&>(
            image_builder.image().words());
        for (std::size_t component = 0; component < kComponents; ++component) {
            const auto polynomial = hpu::seal_adapter::ciphertext_component_to_hpu(
                input, component, *bundle.context);
            require(polynomial.modulus_ids
                        == prepared_input.components[component].modulus_ids,
                    "CMB013 input modulus order differs from the reserved input");
            for (std::size_t basis = 0;
                 basis < polynomial.modulus_ids.size(); ++basis) {
                const auto first = polynomial.words.begin()
                    + static_cast<std::ptrdiff_t>(basis * kDegree);
                const auto span = prepared_input.components[component].limbs[basis];
                require(span.line_count * hpu::runtime::kHpuMemLineWords == kDegree,
                        "CMB013 input limb is not one exact N4096 allocation");
                const auto destination = initial_image.begin()
                    + static_cast<std::ptrdiff_t>(
                        span.line_offset * hpu::runtime::kHpuMemLineWords);
                std::copy(first, first + kDegree, destination);
            }
        }

        hpu::seal_adapter::CkksSoftwareExecutor software_executor(
            *bundle.context, image_builder.image());
        software_executor.rescale(
            prepared_input, rescale_constants, output, canonical_twiddles);
        for (std::size_t component = 0; component < kComponents; ++component) {
            const auto seal_words = hpu::seal_adapter::hpu_to_seal_ntt(
                software_executor.export_component(output, component),
                output.parms_id, *bundle.context);
            require(std::equal(
                        seal_words.begin(), seal_words.end(),
                        expected.data(component)),
                    "HPU_SEAL Rescale software result differs from SEAL");
        }

        const auto lowered = hpu::seal_adapter::lower_ckks_operation_plan(
            plan, *bundle.context);
        require(lowered.operations.size() == 1
                    && count_token(lowered.body_asm, "psync") == 1,
                "CMB013 lowering is not one standalone terminal program");
        const auto relocation = hpu::seal_adapter::build_ckks_relocation_schedule(
            lowered, image_builder.image(), *bundle.context);
        require(relocation.complete()
                    && relocation.bindings.size() == relocation.expected_dma_count,
                "CMB013 Rescale DMA relocation is incomplete");
        const auto runtime = hpu::seal_adapter::lower_ckks_runtime_program(
            lowered, relocation);
        require(image_builder.image().used_lines() + kGuardLines
                    <= image_builder.image().capacity_lines(),
                "CMB013 image leaves no room for the AM guard");
        const auto artifacts = hpu::seal_adapter::render_ckks_runtime_artifacts(
            kStem, runtime, image_builder.image().capacity_lines());
        const auto& expected_image = software_executor.memory().words();
        hpu::seal_adapter::write_ckks_delivery_package(
            output_directory, kStem, lowered, runtime, artifacts,
            image_builder.image(), &expected_image);

        std::ostringstream metadata;
        metadata << std::setprecision(17)
                 << "{\n"
                 << "  \"format_version\": 1,\n"
                 << "  \"case_id\": \"HPU_IT_DIR_CMB_013\",\n"
                 << "  \"scheme\": \"CKKS\",\n"
                 << "  \"api\": \"" << kApi << "\",\n"
                 << "  \"producer_commit\": \"" << producer_commit << "\",\n"
                 << "  \"poly_modulus_degree\": " << kDegree << ",\n"
                 << "  \"input_q_count\": " << top.q_moduli.size() << ",\n"
                 << "  \"output_q_count\": " << next.q_moduli.size() << ",\n"
                 << "  \"special_modulus_count\": "
                 << top.special_moduli.size() << ",\n"
                 << "  \"input_component_count\": " << kComponents << ",\n"
                 << "  \"output_component_count\": " << kComponents << ",\n"
                 << "  \"input_chain_index\": " << top.chain_index << ",\n"
                 << "  \"output_chain_index\": " << next.chain_index << ",\n"
                 << "  \"input_scale\": " << input_scale << ",\n"
                 << "  \"output_scale\": " << expected.scale() << ",\n"
                 << "  \"dropped_modulus\": " << top.q_moduli.back() << ",\n"
                 << "  \"semantic_tolerance\": 0.005,\n"
                 << "  \"semantic_error\": " << maximum_error << ",\n"
                 << "  \"domain\": \"canonical_ntt_physical\",\n"
                 << "  \"instruction_count\": " << runtime.instructions.size() << ",\n"
                 << "  \"dma_count\": " << runtime.dma.size() << ",\n"
                 << "  \"image_used_lines\": "
                 << image_builder.image().used_lines() << ",\n"
                 << "  \"guard_lines\": " << kGuardLines << "\n"
                 << "}\n";
        write_text(output_directory / "CMB013_METADATA.json", metadata.str());

        std::cout << std::setprecision(8)
                  << "HPU_SEAL CKKS Rescale: N=" << kDegree
                  << " Q=" << top.q_moduli.size() << "->"
                  << next.q_moduli.size()
                  << " components=" << input.size()
                  << " scale=" << input_scale << "->" << expected.scale()
                  << " instructions=" << runtime.instructions.size()
                  << " dma=" << runtime.dma.size()
                  << " used_lines=" << image_builder.image().used_lines()
                  << " semantic_error=" << maximum_error << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HPU_SEAL CMB013 generation failed: " << error.what() << '\n';
        return 1;
    }
}
