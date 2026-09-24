#include "hpu/runtime/memory_image.hpp"
#include "hpu/seal/bfv_application_image.hpp"
#include "hpu/seal/bfv_operation_codegen.hpp"
#include "hpu/seal/bfv_operation_plan.hpp"
#include "hpu/seal/bfv_operation_relocation.hpp"
#include "hpu/seal/bfv_operation_runtime.hpp"
#include "hpu/seal/bfv_software_executor.hpp"
#include "scheme/bfv/galois.hpp"

#include <seal/seal.h>

#include <algorithm>
#include <array>
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

constexpr const char* kStem = "rotate";
constexpr std::size_t kDegree = 4096;
constexpr std::size_t kComponents = 2;
constexpr std::size_t kExpectedQ = 4;
constexpr int kRotationSteps = 1;
constexpr std::uint64_t kImageCapacityLines = 32768;
constexpr std::uint64_t kGuardLines = 64;
constexpr std::uint32_t kOutputPoison = UINT32_C(0xC0140000);
constexpr std::uint32_t kGuardPoison = UINT32_C(0xA5A51400);

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

template <typename T>
void write_values(const std::filesystem::path& path, const std::vector<T>& values)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() * sizeof(T)));
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
    parameters.set_coeff_modulus(
        ::seal::CoeffModulus::Create(kDegree, {27, 27, 27, 27, 27}));
    parameters.set_plain_modulus(::seal::PlainModulus::Batching(kDegree, 17));
    const ::seal::prng_seed_type seed{
        UINT64_C(0x434D42303134), UINT64_C(0x524F54415445),
        UINT64_C(1), UINT64_C(2), UINT64_C(3), UINT64_C(4),
        UINT64_C(5), UINT64_C(6)};
    parameters.set_random_generator(
        std::make_shared<::seal::Blake2xbPRNGFactory>(seed));
    auto context = std::make_shared<::seal::SEALContext>(
        std::move(parameters), true, ::seal::sec_level_type::none);
    require(context->parameters_set(),
            "SEAL rejected the deterministic CMB014 BFV parameters");
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

void require_contiguous_output(const hpu::seal_adapter::PreparedBfvRnsObject& output,
                               std::uint64_t first_line, std::uint64_t line_count)
{
    std::uint64_t cursor = first_line;
    for (const auto& component : output.components) {
        for (const auto& limb : component.limbs) {
            require(limb.line_offset == cursor && limb.line_count == kDegree / 64,
                    "BFV Rotate output limbs are not contiguous N4096 polynomials");
            cursor += limb.line_count;
        }
    }
    require(cursor == first_line + line_count,
            "BFV Rotate output span length is inconsistent");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 3) {
            throw std::invalid_argument(
                "usage: hpu_seal_cmb014_generator OUTPUT_DIR PRODUCER_COMMIT");
        }
        const std::filesystem::path output_directory(argv[1]);
        const std::string producer_commit(argv[2]);
        require(producer_commit.size() == 40 &&
                    std::all_of(producer_commit.begin(), producer_commit.end(), [](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                    }),
                "producer commit must be a full lowercase SHA-1");

        const auto context = create_context();
        const auto first_data = context->first_context_data();
        require(first_data != nullptr, "BFV first data level is missing");
        const auto& parameters = first_data->parms();
        const auto& moduli = parameters.coeff_modulus();
        require(moduli.size() == kExpectedQ,
                "BFV CMB014 requires Q4 plus one special prime");
        const std::uint64_t plain_modulus = parameters.plain_modulus().value();
        const std::uint32_t galois_element =
            hpu::scheme::bfv::row_rotation_galois_element(kDegree, kRotationSteps);

        ::seal::KeyGenerator key_generator(*context);
        ::seal::PublicKey public_key;
        ::seal::GaloisKeys galois_keys;
        key_generator.create_public_key(public_key);
        key_generator.create_galois_keys(
            std::vector<std::uint32_t>{galois_element}, galois_keys);

        ::seal::BatchEncoder encoder(*context);
        const std::size_t slot_count = encoder.slot_count();
        const std::size_t row_size = slot_count / 2;
        std::vector<std::uint64_t> input_slots(slot_count);
        std::vector<std::uint64_t> expected_slots(slot_count);
        for (std::size_t index = 0; index < slot_count; ++index) {
            input_slots[index] =
                (3U * index * index + 5U * index + 7U) % plain_modulus;
        }
        for (std::size_t index = 0; index < slot_count; ++index) {
            const std::size_t row = index / row_size;
            const std::size_t column = index % row_size;
            expected_slots[index] =
                input_slots[row * row_size + (column + kRotationSteps) % row_size];
        }

        ::seal::Plaintext plaintext;
        encoder.encode(input_slots, plaintext);
        ::seal::Encryptor encryptor(*context, public_key);
        ::seal::Ciphertext encrypted;
        encryptor.encrypt(plaintext, encrypted);
        ::seal::Evaluator evaluator(*context);
        ::seal::Ciphertext expected;
        evaluator.rotate_rows(encrypted, kRotationSteps, galois_keys, expected);
        ::seal::Decryptor decryptor(*context, key_generator.secret_key());
        ::seal::Plaintext decrypted;
        std::vector<std::uint64_t> actual_slots;
        decryptor.decrypt(expected, decrypted);
        encoder.decode(decrypted, actual_slots);
        require(actual_slots == expected_slots,
                "SEAL RotateRows result differs from the fixed slot oracle");

        hpu::seal_adapter::BfvApplicationImageBuilder image_builder(
            *context, kImageCapacityLines);
        image_builder.add_modulus_table();
        const auto canonical_twiddles = image_builder.add_canonical_twiddles();
        const auto& level = image_builder.level_chain().top();
        const auto input = image_builder.add_ciphertext("input/x", encrypted);
        const auto keyswitch_constants =
            image_builder.add_keyswitch_constants("constants/keyswitch/top", level);
        const auto rotation_key = image_builder.add_row_rotation_key(
            "key/rotate_rows_1/top", galois_keys, kRotationSteps, level);
        const auto fused_twiddles = image_builder.add_row_rotation_twiddles(
            "rotate_rows_1/top", kRotationSteps, level);
        const auto workspace = image_builder.reserve_ciphertext(
            "scratch/rotate_rows_1", level, kComponents,
            hpu::runtime::PolynomialDomain::coefficient, galois_element);

        hpu::seal_adapter::BfvOperationPlan plan(image_builder);
        const auto output = plan.append_rotate_rows(
            "rotate_rows_1", input, kRotationSteps, rotation_key,
            keyswitch_constants, fused_twiddles, workspace, "output/rotate");
        require(plan.steps().size() == 1 &&
                    plan.steps().front().kind ==
                        hpu::seal_adapter::BfvOperationKind::rotate_rows &&
                    output.parms_id == level.parms_id && output.key_domain == 1,
                "BFV CMB014 plan is not one RotateRows operation");

        hpu::seal_adapter::BfvSoftwareExecutor software_executor(
            *context, image_builder.image());
        software_executor.rotate_rows(
            input, kRotationSteps, rotation_key, keyswitch_constants,
            fused_twiddles, canonical_twiddles, workspace, output);
        for (std::size_t component = 0; component < kComponents; ++component) {
            const auto actual = software_executor.export_component(output, component);
            require(std::equal(actual.words.begin(), actual.words.end(),
                               expected.data(component)),
                    "HPU_SEAL BFV Rotate software result differs from SEAL");
        }

        const auto lowered =
            hpu::seal_adapter::lower_bfv_operation_plan(plan, *context);
        const auto relocation = hpu::seal_adapter::build_bfv_relocation_schedule(
            lowered, image_builder.image(), *context);
        require(relocation.complete(), "BFV Rotate DMA relocation is incomplete");
        const auto runtime =
            hpu::seal_adapter::lower_bfv_runtime_program(lowered, relocation);

        const std::uint64_t active_lines = image_builder.image().used_lines();
        const std::uint64_t output_lines =
            kComponents * kExpectedQ * (kDegree / 64);
        const std::uint64_t output_offset =
            output.components.front().limbs.front().line_offset;
        require_contiguous_output(output, output_offset, output_lines);
        require(output_offset + output_lines == active_lines,
                "BFV Rotate output is not the final active HPU_MEM allocation");
        const std::uint64_t total_lines = active_lines + kGuardLines;
        const auto artifacts = hpu::seal_adapter::render_bfv_runtime_artifacts(
            kStem, runtime, total_lines);

        std::vector<std::uint32_t> window = image_builder.image().words();
        require(window.size() == active_lines * hpu::runtime::kHpuMemLineWords,
                "BFV Rotate HPU_MEM image has inconsistent line geometry");
        const std::size_t first_output_word = static_cast<std::size_t>(
            output_offset * hpu::runtime::kHpuMemLineWords);
        const std::size_t output_word_count = static_cast<std::size_t>(
            output_lines * hpu::runtime::kHpuMemLineWords);
        for (std::size_t index = 0; index < output_word_count; ++index) {
            window[first_output_word + index] =
                kOutputPoison ^ static_cast<std::uint32_t>(index);
        }
        const std::size_t guard_words = static_cast<std::size_t>(
            kGuardLines * hpu::runtime::kHpuMemLineWords);
        for (std::size_t index = 0; index < guard_words; ++index) {
            window.push_back(kGuardPoison ^ static_cast<std::uint32_t>(index));
        }
        const auto golden = ciphertext_words(expected, *context);
        require(golden.size() == output_word_count,
                "BFV Rotate golden size differs from output allocation");

        std::filesystem::create_directories(output_directory);
        write_text(output_directory / "rotate.asm", lowered.body_asm);
        std::string inst32;
        std::string command26;
        for (const auto& instruction : runtime.instructions) {
            inst32 += std::bitset<32>(instruction.word).to_string() + '\n';
            command26 += std::bitset<26>(instruction.command26).to_string() + '\n';
        }
        write_text(output_directory / "rotate.inst32", inst32);
        write_text(output_directory / "rotate.cmd26", command26);
        write_text(output_directory / "rotate.h", artifacts.header);
        write_text(output_directory / "rotate.c", artifacts.source);
        write_text(output_directory / "resolved_dma.csv",
                   artifacts.resolved_dma_manifest);
        write_values(output_directory / "window.u32.bin", window);
        write_values(output_directory / "golden.u32.bin", golden);
        write_values(output_directory / "input_slots.u64.bin", input_slots);
        write_values(output_directory / "expected_slots.u64.bin", expected_slots);

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
        for (const auto& modulus : moduli) {
            q_moduli.push_back(static_cast<std::uint32_t>(modulus.value()));
        }
        std::ostringstream metadata;
        metadata << "{\n"
                 << "  \"format_version\": 1,\n"
                 << "  \"case_id\": \"HPU_IT_DIR_CMB_014\",\n"
                 << "  \"scheme\": \"BFV\",\n"
                 << "  \"api\": \"hpu::seal_adapter::BfvOperationPlan::append_rotate_rows\",\n"
                 << "  \"producer_commit\": \"" << producer_commit << "\",\n"
                 << "  \"poly_modulus_degree\": " << kDegree << ",\n"
                 << "  \"component_count\": " << kComponents << ",\n"
                 << "  \"q_count\": " << q_moduli.size() << ",\n"
                 << "  \"q_moduli\": " << json_array(q_moduli) << ",\n"
                 << "  \"plain_modulus\": " << plain_modulus << ",\n"
                 << "  \"domain\": \"coefficient\",\n"
                 << "  \"rotation_steps\": " << kRotationSteps << ",\n"
                 << "  \"galois_element\": " << galois_element << ",\n"
                 << "  \"instruction_count\": " << runtime.instructions.size() << ",\n"
                 << "  \"dma_count\": " << runtime.dma.size() << ",\n"
                 << "  \"active_lines\": " << active_lines << ",\n"
                 << "  \"output_offset\": " << output_offset << ",\n"
                 << "  \"output_lines\": " << output_lines << ",\n"
                 << "  \"guard_offset\": " << active_lines << ",\n"
                 << "  \"guard_lines\": " << kGuardLines << ",\n"
                 << "  \"total_lines\": " << total_lines << "\n"
                 << "}\n";
        write_text(output_directory / "metadata.json", metadata.str());
        write_text(output_directory / "producer_commit.txt", producer_commit + "\n");

        std::cout << "HPU_SEAL BFV RotateRows: N=" << kDegree
                  << " Q=" << q_moduli.size()
                  << " steps=" << kRotationSteps
                  << " galois=" << galois_element
                  << " instructions=" << runtime.instructions.size()
                  << " dma=" << runtime.dma.size()
                  << " active_lines=" << active_lines
                  << " total_lines=" << total_lines << '\n'
                  << "slot_oracle=PASS ciphertext_oracle=PASS software_executor=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HPU_SEAL CMB014 generation failed: " << error.what() << '\n';
        return 1;
    }
}
