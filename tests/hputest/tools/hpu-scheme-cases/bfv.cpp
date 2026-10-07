#include "fixture.hpp"
#include "hpu/seal/application_delivery.hpp"
#include "hpu/seal/bfv_application_image.hpp"
#include "hpu/seal/bfv_operation_codegen.hpp"
#include "hpu/seal/bfv_operation_plan.hpp"
#include "hpu/seal/bfv_operation_relocation.hpp"
#include "hpu/seal/bfv_software_executor.hpp"
#include "scheme/bfv/galois.hpp"

void generate_bfv(Fixture &f) {
    using namespace hpu::seal_adapter;
    BfvApplicationImageBuilder builder(*f.context, 16777216);
    builder.add_modulus_table();
    const auto &level = builder.level_chain().top();
    const auto input = builder.add_ciphertext("input/x", f.input);
    BfvOperationPlan plan(builder);
    PreparedBfvRnsObject output, right, workspace;
    PreparedEvaluationKey key;
    PreparedKeySwitchConstants constants;
    PreparedBfvMultiplyConstants multiply;
    PreparedBfvModSwitchConstants modswitch;
    std::vector<PreparedCanonicalTwiddles> tables;
    std::vector<PreparedFusedAutomorphismTwiddles> fused;
    if (f.operation == "hadd" || f.operation == "hmul") {
        right = builder.add_ciphertext("input/right", f.right);
        if (f.operation == "hadd") output = plan.append_add("tested", input, right, "output");
        else {
            tables = builder.add_canonical_twiddles();
            key = builder.add_relinearization_key("key/reline", f.relin, level);
            constants = builder.add_keyswitch_constants("constants/keyswitch", level);
            multiply = builder.add_multiply_constants("constants/multiply", level);
            output = plan.append_multiply("tested", input, right, key, constants, multiply, "output");
        }
    } else if (f.operation == "keyswitch" || f.operation == "reline") {
        tables = builder.add_canonical_twiddles();
        key = builder.add_relinearization_key("key/reline", f.relin, level);
        constants = builder.add_keyswitch_constants("constants/keyswitch", level);
        output = plan.append_relinearize("tested", input, key, constants, "output");
    } else if (f.operation == "modswitch") {
        modswitch = builder.add_mod_switch_constants("constants/modswitch", level);
        output = plan.append_mod_switch("tested", input, modswitch, "output");
    } else {
        tables = builder.add_canonical_twiddles();
        key = builder.add_row_rotation_key("key/rotate", f.galois, f.rotation_steps, level);
        constants = builder.add_keyswitch_constants("constants/keyswitch", level);
        fused = builder.add_row_rotation_twiddles("constants/rotation", f.rotation_steps, level);
        workspace = builder.reserve_ciphertext("scratch/rotate", level, 2,
            hpu::runtime::PolynomialDomain::coefficient,
            hpu::scheme::bfv::row_rotation_galois_element(f.degree, f.rotation_steps),
            hpu::runtime::AllocationKind::workspace);
        output = plan.append_rotate_rows("tested", input, f.rotation_steps, key, constants, fused, workspace, "output");
    }
    builder.trim_capacity_to_used_lines();
    BfvSoftwareExecutor executor(*f.context, builder.image());
    if (f.operation == "hadd") executor.add(input, right, output);
    else if (f.operation == "hmul") executor.multiply(input, right, key, constants, multiply, tables, output);
    else if (f.operation == "keyswitch" || f.operation == "reline")
        executor.relinearize(input, key, constants, tables, output);
    else if (f.operation == "modswitch") executor.mod_switch(input, modswitch, output);
    else executor.rotate_rows(input, f.rotation_steps, key, constants, fused, tables, workspace, output);
    const auto lowered = lower_bfv_operation_plan(plan, *f.context, true, true);
    const auto relocations = build_bfv_relocation_schedule(lowered, builder.image(), *f.context);
    if (!relocations.complete()) throw std::runtime_error("BFV unresolved DMA");
    const auto runtime = lower_bfv_runtime_program(lowered, relocations);
    const auto artifacts = render_bfv_runtime_artifacts(f.stem, runtime, builder.image().capacity_lines());
    auto request = make_bfv_application_package(f.stem, *f.context, lowered, runtime,
        artifacts, builder.image(), {f.expected}, executor.memory().words());
    hpu::delivery::write_application_package(f.directory, request);
}
