#include "fixture.hpp"
#include "hpu/seal/application_delivery.hpp"
#include "hpu/seal/application_image.hpp"
#include "hpu/seal/ntt_bridge.hpp"
#include "hpu/seal/operation_codegen.hpp"
#include "hpu/seal/operation_plan.hpp"
#include "hpu/seal/operation_relocation.hpp"
#include "hpu/seal/software_executor.hpp"
#include "scheme/ckks/galois.hpp"

void generate_ckks(Fixture &f) {
    using namespace hpu::seal_adapter;
    CkksApplicationImageBuilder builder(*f.context, 16777216);
    builder.add_modulus_table();
    const auto &level = builder.level_chain().top();
    PreparedRnsObject input;
    if (f.operation == "keyswitch" || f.operation == "reline" || f.operation == "modswitch") {
        // 这些 kernel 会把外部 NTT 输入转为系数域再写回，输入必须是可写 payload。
        input = builder.reserve_ciphertext("input/x", level, f.input.size(), f.input.scale(),
            hpu::runtime::PolynomialDomain::canonical_ntt_physical, 1,
            hpu::runtime::AllocationKind::workspace);
        auto &words = const_cast<std::vector<std::uint32_t>&>(builder.image().words());
        for (std::size_t c = 0; c < f.input.size(); ++c) {
            const auto polynomial = ciphertext_component_to_hpu(f.input, c, *f.context);
            for (std::size_t q = 0; q < polynomial.modulus_ids.size(); ++q)
                std::copy_n(polynomial.words.begin() + q * f.degree, f.degree,
                    words.begin() + input.components[c].limbs[q].line_offset * 64);
        }
    } else input = builder.add_ciphertext("input/x", f.input);
    CkksOperationPlan plan(builder);
    PreparedRnsObject output, right, workspace, tensor;
    PreparedEvaluationKey key;
    PreparedKeySwitchConstants constants;
    PreparedRescaleConstants rescale;
    std::vector<PreparedCanonicalTwiddles> tables;
    std::vector<PreparedFusedAutomorphismTwiddles> fused;
    if (f.operation == "hadd" || f.operation == "hmul") {
        right = builder.add_ciphertext("input/right", f.right);
        if (f.operation == "hadd") output = plan.append_add("tested", input, right, "output");
        else {
            tables = builder.add_canonical_twiddles();
            key = builder.add_relinearization_key("key/reline", f.relin, level);
            constants = builder.add_keyswitch_constants("constants/keyswitch", level);
            tensor = plan.append_multiply("multiply", input, right, "intermediate/tensor");
            output = plan.append_relinearize("tested", tensor, key, constants, "output");
        }
    } else if (f.operation == "keyswitch" || f.operation == "reline") {
        tables = builder.add_canonical_twiddles();
        key = builder.add_relinearization_key("key/reline", f.relin, level);
        constants = builder.add_keyswitch_constants("constants/keyswitch", level);
        output = plan.append_relinearize("tested", input, key, constants, "output");
    } else if (f.operation == "modswitch") {
        tables = builder.add_canonical_twiddles();
        rescale = builder.add_rescale_constants("constants/rescale", level);
        output = plan.append_rescale("tested", input, rescale, "output");
    } else {
        tables = builder.add_canonical_twiddles();
        key = builder.add_rotation_key("key/rotate", f.galois, f.rotation_steps, level);
        constants = builder.add_keyswitch_constants("constants/keyswitch", level);
        fused = builder.add_rotation_twiddles("constants/rotation", f.rotation_steps, level);
        const auto galois_element = hpu::scheme::ckks::rotation_galois_element(f.degree, f.rotation_steps);
        workspace = builder.reserve_ciphertext("scratch/rotate", level, 2, input.scale,
            hpu::runtime::PolynomialDomain::coefficient, galois_element,
            hpu::runtime::AllocationKind::workspace);
        output = plan.append_rotate_slots("tested", input, f.rotation_steps, key, constants, fused, workspace, "output");
    }
    builder.trim_capacity_to_used_lines();
    CkksSoftwareExecutor executor(*f.context, builder.image());
    if (f.operation == "hadd") executor.add(input, right, output);
    else if (f.operation == "hmul") {
        executor.multiply(input, right, tensor);
        executor.relinearize(tensor, key, constants, output, tables);
    }
    else if (f.operation == "keyswitch" || f.operation == "reline")
        executor.relinearize(input, key, constants, output, tables);
    else if (f.operation == "modswitch") executor.rescale(input, rescale, output, tables);
    else executor.rotate_slots(input, f.rotation_steps, key, constants, fused, tables, workspace, output);
    const auto lowered = lower_ckks_operation_plan(plan, *f.context);
    const auto relocations = build_ckks_relocation_schedule(lowered, builder.image(), *f.context);
    if (!relocations.complete()) throw std::runtime_error("CKKS unresolved DMA");
    const auto runtime = lower_ckks_runtime_program(lowered, relocations);
    const auto artifacts = render_ckks_runtime_artifacts(f.stem, runtime, builder.image().capacity_lines());
    const std::vector<seal::Ciphertext> oracle = f.operation == "hmul" ?
        std::vector<seal::Ciphertext>{f.tensor, f.expected} : std::vector<seal::Ciphertext>{f.expected};
    auto request = make_ckks_application_package(f.stem, *f.context, lowered, runtime,
        artifacts, builder.image(), oracle, executor.memory().words());
    hpu::delivery::write_application_package(f.directory, request);
}
