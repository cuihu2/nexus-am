#include "hpu/runtime/memory_image.hpp"
#include "hpu/seal/bfv_application_image.hpp"
#include "hpu/seal/bfv_context.hpp"
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
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kStem = "hadd";
constexpr std::size_t kDegree = 4096;
constexpr std::size_t kComponents = 2;
constexpr std::size_t kExpectedQ = 4;
constexpr std::uint64_t kImageCapacityLines = 4096;
constexpr std::uint64_t kGuardLines = 64;
constexpr std::uint32_t kOutputPoison = UINT32_C(0xC0DE0000);
constexpr std::uint32_t kGuardPoison = UINT32_C(0xA5A50000);

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

::seal::Ciphertext make_ciphertext(const ::seal::SEALContext& context, std::uint32_t salt)
{
    const auto parms_id = context.first_parms_id();
    const auto context_data = context.get_context_data(parms_id);
    require(context_data != nullptr, "BFV first data level is missing");
    const auto& parameters = context_data->parms();
    require(parameters.poly_modulus_degree() == kDegree,
            "BFV context degree differs from CMB009 contract");

    ::seal::Ciphertext ciphertext;
    ciphertext.resize(context, parms_id, kComponents);
    ciphertext.is_ntt_form() = false;
    ciphertext.scale() = 1.0;
    ciphertext.correction_factor() = 1;

    const auto& moduli = parameters.coeff_modulus();
    for (std::size_t component = 0; component < kComponents; ++component) {
        auto* destination = ciphertext.data(component);
        for (std::size_t basis = 0; basis < moduli.size(); ++basis) {
            const std::uint64_t modulus = moduli[basis].value();
            for (std::size_t index = 0; index < kDegree; ++index) {
                std::uint64_t value =
                    (static_cast<std::uint64_t>(salt) * 65537U +
                     component * 8191U + basis * 4099U + index * 17U + index * index) %
                    modulus;
                switch (index & 1023U) {
                case 0:
                    value = 0;
                    break;
                case 1:
                    value = 1;
                    break;
                case 2:
                    value = modulus - 1;
                    break;
                case 3:
                    value = modulus - 2;
                    break;
                default:
                    break;
                }
                destination[basis * kDegree + index] = value;
            }
        }
    }
    require(::seal::is_valid_for(ciphertext, context),
            "constructed deterministic BFV ciphertext is invalid");
    return ciphertext;
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
                    "BFV HADD output limbs are not contiguous N4096 polynomials");
            cursor += limb.line_count;
        }
    }
    require(cursor == first_line + line_count,
            "BFV HADD output span length is inconsistent");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 3) {
            throw std::invalid_argument(
                "usage: hpu_seal_cmb009_generator OUTPUT_DIR PRODUCER_COMMIT");
        }
        const std::filesystem::path output_directory(argv[1]);
        const std::string producer_commit(argv[2]);
        require(producer_commit.size() == 40 &&
                    std::all_of(producer_commit.begin(), producer_commit.end(), [](char character) {
                        return (character >= '0' && character <= '9') ||
                               (character >= 'a' && character <= 'f');
                    }),
                "producer commit must be a full lowercase SHA-1");

        hpu::seal_adapter::BfvContextSpec spec;
        spec.poly_modulus_degree = kDegree;
        spec.coeff_modulus_bits = {27, 27, 27, 27, 27};
        spec.plain_modulus_bits = 17;
        const auto bundle = hpu::seal_adapter::create_bfv_context(spec);
        const auto left_ciphertext = make_ciphertext(*bundle.context, UINT32_C(0x13579BDF));
        const auto right_ciphertext = make_ciphertext(*bundle.context, UINT32_C(0x2468ACE0));

        ::seal::Evaluator evaluator(*bundle.context);
        ::seal::Ciphertext expected;
        evaluator.add(left_ciphertext, right_ciphertext, expected);

        hpu::seal_adapter::BfvApplicationImageBuilder image_builder(
            *bundle.context, kImageCapacityLines);
        image_builder.add_modulus_table();
        const auto left = image_builder.add_ciphertext("input/left", left_ciphertext);
        const auto right = image_builder.add_ciphertext("input/right", right_ciphertext);
        const auto& top_level = image_builder.level_chain().top();
        require(top_level.q_moduli.size() == kExpectedQ,
                "BFV CMB009 requires Q4 plus one special prime");

        hpu::seal_adapter::BfvOperationPlan plan(image_builder);
        const auto output = plan.append_add("hadd", left, right, "output/hadd");
        require(plan.steps().size() == 1 &&
                    plan.steps().front().kind == hpu::seal_adapter::BfvOperationKind::add,
                "BFV CMB009 plan is not one HADD operation");

        hpu::seal_adapter::BfvSoftwareExecutor software_executor(
            *bundle.context, image_builder.image());
        software_executor.add(left, right, output);
        for (std::size_t component = 0; component < kComponents; ++component) {
            const auto actual = software_executor.export_component(output, component);
            require(std::equal(actual.words.begin(), actual.words.end(), expected.data(component)),
                    "HPU_SEAL BFV HADD software result differs from SEAL");
        }

        const auto lowered = hpu::seal_adapter::lower_bfv_operation_plan(plan, *bundle.context);
        const auto relocation = hpu::seal_adapter::build_bfv_relocation_schedule(
            lowered, image_builder.image(), *bundle.context);
        require(relocation.complete(), "BFV HADD DMA relocation is incomplete");
        const auto runtime = hpu::seal_adapter::lower_bfv_runtime_program(lowered, relocation);

        const std::uint64_t active_lines = image_builder.image().used_lines();
        const std::uint64_t output_lines = kComponents * kExpectedQ * (kDegree / 64);
        const std::uint64_t output_offset = output.components.front().limbs.front().line_offset;
        require_contiguous_output(output, output_offset, output_lines);
        require(output_offset + output_lines == active_lines,
                "BFV HADD output is not the final active HPU_MEM allocation");
        const std::uint64_t total_lines = active_lines + kGuardLines;
        const auto artifacts = hpu::seal_adapter::render_bfv_runtime_artifacts(
            kStem, runtime, total_lines);

        std::vector<std::uint32_t> window = image_builder.image().words();
        require(window.size() == active_lines * hpu::runtime::kHpuMemLineWords,
                "BFV HADD HPU_MEM image has inconsistent line geometry");
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
        const auto golden = ciphertext_words(expected, *bundle.context);
        require(golden.size() == output_word_count,
                "BFV HADD golden size differs from output allocation");

        std::filesystem::create_directories(output_directory);
        write_text(output_directory / "hadd.asm", lowered.body_asm);
        std::string inst32;
        std::string command26;
        for (const auto& instruction : runtime.instructions) {
            inst32 += std::bitset<32>(instruction.word).to_string() + '\n';
            command26 += std::bitset<26>(instruction.command26).to_string() + '\n';
        }
        write_text(output_directory / "hadd.inst32", inst32);
        write_text(output_directory / "hadd.cmd26", command26);
        write_text(output_directory / "hadd.h", artifacts.header);
        write_text(output_directory / "hadd.c", artifacts.source);
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

        std::ostringstream metadata;
        metadata << "{\n"
                 << "  \"format_version\": 1,\n"
                 << "  \"case_id\": \"HPU_IT_DIR_CMB_009\",\n"
                 << "  \"scheme\": \"BFV\",\n"
                 << "  \"api\": \"hpu::seal_adapter::BfvOperationPlan::append_add\",\n"
                 << "  \"producer_commit\": \"" << producer_commit << "\",\n"
                 << "  \"poly_modulus_degree\": " << kDegree << ",\n"
                 << "  \"component_count\": " << kComponents << ",\n"
                 << "  \"q_count\": " << top_level.q_moduli.size() << ",\n"
                 << "  \"q_moduli\": " << json_array(top_level.q_moduli) << ",\n"
                 << "  \"domain\": \"coefficient\",\n"
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

        std::cout << "HPU_SEAL BFV HADD: N=" << kDegree << " Q=" << top_level.q_moduli.size()
                  << " instructions=" << runtime.instructions.size()
                  << " dma=" << runtime.dma.size() << " active_lines=" << active_lines
                  << " total_lines=" << total_lines << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HPU_SEAL CMB009 generation failed: " << error.what() << '\n';
        return 1;
    }
}
