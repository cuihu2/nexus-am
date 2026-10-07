#include "fixture.hpp"
#include "hpu/seal/application_delivery.hpp"
#include "hpu/seal/bgv_operation_plan.hpp"
#include "hpu/seal/bgv_software_executor.hpp"

void generate_bgv(Fixture &f) {
    using namespace hpu::seal_adapter;
    BgvOperationPlan plan(*f.context);
    const auto input = plan.add_ciphertext("input/x", f.input);
    BgvPlannedValue output;
    if (f.operation == "hadd" || f.operation == "hmul") {
        const auto right = plan.add_ciphertext("input/right", f.right);
        output = f.operation == "hadd" ? plan.append_add("tested", input, right) :
                                       plan.append_multiply_relinearize("tested", input, right, f.relin);
    } else if (f.operation == "keyswitch" || f.operation == "reline")
        output = plan.append_relinearize("tested", input, f.relin);
    else if (f.operation == "modswitch") output = plan.append_modswitch_to_next("tested", input);
    else output = plan.append_rotate_rows("tested", input, f.rotation_steps, f.galois);
    plan.set_output(output);
    const auto application = plan.lower(16777216);
    BgvSoftwareExecutor executor(*f.context, application.image);
    executor.execute(plan);
    auto request = make_bgv_application_package(f.stem, *f.context, plan, application,
        {f.expected}, executor.memory().words());
    hpu::delivery::write_application_package(f.directory, request);
}
