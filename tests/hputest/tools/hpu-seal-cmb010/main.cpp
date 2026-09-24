#include "hpu/runtime/memory_image.hpp"
#include "hpu/seal/bfv_application_image.hpp"
#include "hpu/seal/bfv_operation_codegen.hpp"
#include "hpu/seal/bfv_operation_plan.hpp"
#include "hpu/seal/bfv_operation_relocation.hpp"
#include "hpu/seal/bfv_operation_runtime.hpp"
#include "hpu/seal/bfv_software_executor.hpp"

#include <seal/seal.h>

#include <algorithm>
#include <bitset>
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

constexpr const char* kStem = "hmul";
constexpr std::size_t kDegree = 4096;
constexpr std::size_t kComponents = 2;
constexpr std::size_t kExpectedQ = 4;
constexpr std::uint64_t kGuardLines = 64;
constexpr std::uint64_t kImageCapacityLines = 65536 - kGuardLines;
constexpr std::uint32_t kOutputPoison = UINT32_C(0xC0DE1000);
constexpr std::uint32_t kGuardPoison = UINT32_C(0xA5A51000);

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream output(path);
    if (!output || !(output << contents)) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

void write_words(const std::filesystem::path& path, const std::vector<std::uint32_t>& words)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(words.data()),
                 static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
    if (!output) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

const char* allocation_kind(hpu::runtime::AllocationKind kind)
{
    using Kind = hpu::runtime::AllocationKind;
    switch (kind) {
    case Kind::modulus_table:
        return "modulus_table";
    case Kind::constant:
        return "constant";
    case Kind::ciphertext:
        return "ciphertext";
    case Kind::plaintext:
        return "plaintext";
    case Kind::evaluation_key:
        return "evaluation_key";
    case Kind::twiddle:
        return "twiddle";
    case Kind::workspace:
        return "workspace";
    case Kind::output:
        return "output";
    }
    throw std::logic_error("unknown HPU_MEM allocation kind");
}

std::shared_ptr<::seal::SEALContext> create_context()
{
    ::seal::EncryptionParameters parameters(::seal::scheme_type::bfv);
    parameters.set_poly_modulus_degree(kDegree);
    parameters.set_coeff_modulus(::seal::CoeffModulus::Create(kDegree, {27, 27, 27, 27, 27}));
    parameters.set_plain_modulus(::seal::PlainModulus::Batching(kDegree, 17));
    const ::seal::prng_seed_type seed{
        UINT64_C(0x434D423031304850), UINT64_C(0x555345414C424656),
        UINT64_C(0x4D554C5449504C59), UINT64_C(0x0000000000000001),
        UINT64_C(0x0000000000000002), UINT64_C(0x0000000000000003),
        UINT64_C(0x0000000000000004), UINT64_C(0x0000000000000005)};
    parameters.set_random_generator(std::make_shared<::seal::Blake2xbPRNGFactory>(seed));
    auto context = std::make_shared<::seal::SEALContext>(
        std::move(parameters), true, ::seal::sec_level_type::none);
    require(context->parameters_set(),
            std::string("SEAL rejected the fixed CMB010 BFV parameters: ") +
                context->parameter_error_message());
    return context;
}

std::vector<std::uint32_t> ciphertext_words(const ::seal::Ciphertext& ciphertext,
                                            const ::seal::SEALContext& context)
{
    const auto context_data = context.get_context_data(ciphertext.parms_id());
    require(context_data != nullptr, "ciphertext context data is missing");
    const auto& moduli = context_data->parms().coeff_modulus();
    std::vector<std::uint32_t> result;
    result.reserve(ciphertext.size() * moduli.size() * kDegree);
    for (std::size_t component = 0; component < ciphertext.size(); ++component) {
        const auto* source = ciphertext.data(component);
        for (std::size_t basis = 0; basis < moduli.size(); ++basis) {
            for (std::size_t index = 0; index < kDegree; ++index) {
                const std::uint64_t word = source[basis * kDegree + index];
                require(word < moduli[basis].value() && word <= UINT32_MAX,
                        "BFV golden coefficient exceeds the HPU uint32 ABI");
                result.push_back(static_cast<std::uint32_t>(word));
            }
        }
    }
    return result;
}

std::string json_array(const std::vector<std::uint32_t>& values)
{
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << values[index];
    }
    output << ']';
    return output.str();
}

std::uint64_t fnv1a64(const std::vector<std::uint32_t>& words)
{
    std::uint64_t value = UINT64_C(14695981039346656037);
    for (std::uint32_t word : words) {
        for (unsigned byte = 0; byte < sizeof(word); ++byte) {
            value ^= static_cast<std::uint8_t>(word >> (byte * 8));
            value *= UINT64_C(1099511628211);
        }
    }
    return value;
}

void require_contiguous_output(const hpu::seal_adapter::PreparedBfvRnsObject& output,
                               std::uint64_t first_line, std::uint64_t line_count)
{
    std::uint64_t cursor = first_line;
    for (const auto& component : output.components) {
        for (const auto& limb : component.limbs) {
            require(limb.line_offset == cursor && limb.line_count == kDegree / 64,
                    "BFV HMUL output limbs are not contiguous N4096 polynomials");
            cursor += limb.line_count;
        }
    }
    require(cursor == first_line + line_count,
            "BFV HMUL output span length is inconsistent");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 3) {
            throw std::invalid_argument(
                "usage: hpu_seal_cmb010_generator OUTPUT_DIR PRODUCER_COMMIT");
        }
        const std::filesystem::path output_directory(argv[1]);
        const std::string producer_commit(argv[2]);
        require(producer_commit.size() == 40 &&
                    std::all_of(producer_commit.begin(), producer_commit.end(), [](char character) {
                        return (character >= '0' && character <= '9') ||
                               (character >= 'a' && character <= 'f');
                    }),
                "producer commit must be a full lowercase SHA-1");

        const auto context = create_context();
        const auto first_data = context->first_context_data();
        require(first_data != nullptr, "BFV first data level is missing");
        const auto& level_parameters = first_data->parms();
        require(level_parameters.poly_modulus_degree() == kDegree &&
                    level_parameters.coeff_modulus().size() == kExpectedQ,
                "BFV context differs from the fixed CMB010 N4096/Q4 contract");

        ::seal::KeyGenerator key_generator(*context);
        ::seal::PublicKey public_key;
        ::seal::RelinKeys relinearization_keys;
        key_generator.create_public_key(public_key);
        key_generator.create_relin_keys(relinearization_keys);

        ::seal::BatchEncoder encoder(*context);
        std::vector<std::uint64_t> left_slots(encoder.slot_count());
        std::vector<std::uint64_t> right_slots(encoder.slot_count());
        std::vector<std::uint64_t> expected_slots(encoder.slot_count());
        const std::uint64_t plain_modulus = level_parameters.plain_modulus().value();
        for (std::size_t index = 0; index < encoder.slot_count(); ++index) {
            left_slots[index] = (index * 17U + 3U) % plain_modulus;
            right_slots[index] = (index * index + 5U) % plain_modulus;
            expected_slots[index] = left_slots[index] * right_slots[index] % plain_modulus;
        }
        ::seal::Plaintext left_plaintext;
        ::seal::Plaintext right_plaintext;
        encoder.encode(left_slots, left_plaintext);
        encoder.encode(right_slots, right_plaintext);
        ::seal::Encryptor encryptor(*context, public_key);
        ::seal::Ciphertext left_ciphertext;
        ::seal::Ciphertext right_ciphertext;
        encryptor.encrypt(left_plaintext, left_ciphertext);
        encryptor.encrypt(right_plaintext, right_ciphertext);

        ::seal::Evaluator evaluator(*context);
        ::seal::Ciphertext expected;
        evaluator.multiply(left_ciphertext, right_ciphertext, expected);
        evaluator.relinearize_inplace(expected, relinearization_keys);
        require(expected.size() == kComponents && !expected.is_ntt_form(),
                "modified-SEAL HMUL did not produce a two-component coefficient ciphertext");
        ::seal::Decryptor decryptor(*context, key_generator.secret_key());
        ::seal::Plaintext decrypted;
        decryptor.decrypt(expected, decrypted);
        std::vector<std::uint64_t> decoded;
        encoder.decode(decrypted, decoded);
        require(decoded.size() >= expected_slots.size() &&
                    std::equal(expected_slots.begin(), expected_slots.end(), decoded.begin()),
                "modified-SEAL HMUL oracle produced incorrect BFV slots");

        hpu::seal_adapter::BfvApplicationImageBuilder image_builder(
            *context, kImageCapacityLines);
        image_builder.add_modulus_table();
        const auto canonical_twiddles = image_builder.add_canonical_twiddles();
        const auto& top_level = image_builder.level_chain().top();
        const auto left = image_builder.add_ciphertext("input/left", left_ciphertext);
        const auto right = image_builder.add_ciphertext("input/right", right_ciphertext);
        const auto relinearization_key = image_builder.add_relinearization_key(
            "key/relinearization/top", relinearization_keys, top_level);
        const auto keyswitch_constants = image_builder.add_keyswitch_constants(
            "constants/keyswitch/top", top_level);
        const auto multiply_constants = image_builder.add_multiply_constants(
            "constants/multiply/top", top_level);

        hpu::seal_adapter::BfvOperationPlan plan(image_builder);
        const auto output = plan.append_multiply(
            "hmul", left, right, relinearization_key, keyswitch_constants,
            multiply_constants, "output/hmul");
        require(plan.steps().size() == 1 &&
                    plan.steps().front().kind == hpu::seal_adapter::BfvOperationKind::multiply &&
                    output.components.size() == kComponents &&
                    output.domain == hpu::runtime::PolynomialDomain::coefficient,
                "BFV CMB010 plan is not one fused HMUL/relinearize operation");

        hpu::seal_adapter::BfvSoftwareExecutor software_executor(
            *context, image_builder.image());
        software_executor.multiply(left, right, relinearization_key, keyswitch_constants,
                                   multiply_constants, canonical_twiddles, output);
        for (std::size_t component = 0; component < kComponents; ++component) {
            const auto actual = software_executor.export_component(output, component);
            require(std::equal(actual.words.begin(), actual.words.end(), expected.data(component)),
                    "HPU_SEAL BFV HMUL software result differs from modified-SEAL");
        }

        const auto lowered = hpu::seal_adapter::lower_bfv_operation_plan(plan, *context);
        const auto relocation = hpu::seal_adapter::build_bfv_relocation_schedule(
            lowered, image_builder.image(), *context);
        require(relocation.complete(), "BFV HMUL DMA relocation is incomplete");
        const auto runtime = hpu::seal_adapter::lower_bfv_runtime_program(lowered, relocation);

        const std::uint64_t active_lines = image_builder.image().used_lines();
        const std::uint64_t output_lines =
            kComponents * kExpectedQ * (kDegree / hpu::runtime::kHpuMemLineWords);
        const std::uint64_t output_offset = output.components.front().limbs.front().line_offset;
        require_contiguous_output(output, output_offset, output_lines);
        require(output_offset + output_lines == active_lines,
                "BFV HMUL output is not the final active HPU_MEM allocation");
        const std::uint64_t total_lines = active_lines + kGuardLines;
        require(total_lines <= 65536, "BFV HMUL image exceeds the 65536-line HPU window");
        const auto artifacts = hpu::seal_adapter::render_bfv_runtime_artifacts(
            kStem, runtime, total_lines);

        std::vector<std::uint32_t> window = image_builder.image().words();
        require(window.size() == active_lines * hpu::runtime::kHpuMemLineWords,
                "BFV HMUL HPU_MEM image has inconsistent line geometry");
        const std::size_t first_output_word =
            static_cast<std::size_t>(output_offset * hpu::runtime::kHpuMemLineWords);
        const std::size_t output_word_count =
            static_cast<std::size_t>(output_lines * hpu::runtime::kHpuMemLineWords);
        for (std::size_t index = 0; index < output_word_count; ++index) {
            window[first_output_word + index] = kOutputPoison ^ static_cast<std::uint32_t>(index);
        }
        const std::size_t guard_words =
            static_cast<std::size_t>(kGuardLines * hpu::runtime::kHpuMemLineWords);
        for (std::size_t index = 0; index < guard_words; ++index) {
            window.push_back(kGuardPoison ^ static_cast<std::uint32_t>(index));
        }
        const auto golden = ciphertext_words(expected, *context);
        require(golden.size() == output_word_count,
                "BFV HMUL golden size differs from output allocation");

        std::filesystem::create_directories(output_directory);
        write_text(output_directory / "hmul.asm", lowered.body_asm);
        std::string inst32;
        std::string command26;
        for (const auto& instruction : runtime.instructions) {
            inst32 += std::bitset<32>(instruction.word).to_string() + '\n';
            command26 += std::bitset<26>(instruction.command26).to_string() + '\n';
        }
        write_text(output_directory / "hmul.inst32", inst32);
        write_text(output_directory / "hmul.cmd26", command26);
        write_text(output_directory / "hmul.h", artifacts.header);
        write_text(output_directory / "hmul.c", artifacts.source);
        write_text(output_directory / "resolved_dma.csv", artifacts.resolved_dma_manifest);
        write_words(output_directory / "window.u32.bin", window);
        write_words(output_directory / "golden.u32.bin", golden);

        std::ostringstream allocations;
        allocations << "id,line_offset,line_count,word_count,kind,read_only\n";
        for (const auto& allocation : image_builder.image().allocations()) {
            allocations << allocation.id << ',' << allocation.span.line_offset << ','
                        << allocation.span.line_count << ',' << allocation.word_count << ','
                        << allocation_kind(allocation.kind) << ','
                        << (allocation.read_only ? 1 : 0) << '\n';
        }
        write_text(output_directory / "allocations.csv", allocations.str());

        std::vector<std::uint32_t> q_moduli;
        for (const auto& modulus : level_parameters.coeff_modulus()) {
            q_moduli.push_back(static_cast<std::uint32_t>(modulus.value()));
        }
        std::ostringstream metadata;
        metadata << "{\n"
                 << "  \"format_version\": 1,\n"
                 << "  \"case_id\": \"HPU_IT_DIR_CMB_010\",\n"
                 << "  \"scheme\": \"BFV\",\n"
                 << "  \"api\": \"hpu::seal_adapter::BfvOperationPlan::append_multiply\",\n"
                 << "  \"producer_commit\": \"" << producer_commit << "\",\n"
                 << "  \"poly_modulus_degree\": " << kDegree << ",\n"
                 << "  \"component_count\": " << kComponents << ",\n"
                 << "  \"q_count\": " << q_moduli.size() << ",\n"
                 << "  \"q_moduli\": " << json_array(q_moduli) << ",\n"
                 << "  \"plain_modulus\": " << plain_modulus << ",\n"
                 << "  \"domain\": \"coefficient\",\n"
                 << "  \"instruction_count\": " << runtime.instructions.size() << ",\n"
                 << "  \"dma_count\": " << runtime.dma.size() << ",\n"
                 << "  \"active_lines\": " << active_lines << ",\n"
                 << "  \"output_offset\": " << output_offset << ",\n"
                 << "  \"output_lines\": " << output_lines << ",\n"
                 << "  \"guard_offset\": " << active_lines << ",\n"
                 << "  \"guard_lines\": " << kGuardLines << ",\n"
                 << "  \"total_lines\": " << total_lines << ",\n"
                 << "  \"window_fnv1a64\": \"0x" << std::hex << std::setw(16)
                 << std::setfill('0') << fnv1a64(window) << "\",\n"
                 << "  \"golden_fnv1a64\": \"0x" << std::setw(16)
                 << fnv1a64(golden) << "\"\n" << std::dec
                 << "}\n";
        write_text(output_directory / "metadata.json", metadata.str());
        write_text(output_directory / "producer_commit.txt", producer_commit + "\n");

        std::cout << "HPU_SEAL BFV HMUL: N=" << kDegree << " Q=" << q_moduli.size()
                  << " instructions=" << runtime.instructions.size()
                  << " dma=" << runtime.dma.size() << " active_lines=" << active_lines
                  << " total_lines=" << total_lines << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HPU_SEAL CMB010 generation failed: " << error.what() << '\n';
        return 1;
    }
}
