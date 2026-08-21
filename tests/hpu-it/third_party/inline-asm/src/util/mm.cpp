#include "util/mm.hpp"
#include "util/hpu_asm.hpp"

#include <sstream>
#include <string>

std::string generate_hpu_mm_body_asm(
    int obj_a,
    int obj_b,
    int obj_c,
    bool append_psync)
{
    std::ostringstream asm_code;
    asm_code << hpu::pmul(obj_a, obj_b, obj_c);
    if (append_psync) {
        asm_code << hpu::psync();
    }
    return asm_code.str();
}

std::string generate_hpu_mm_asm(
    int obj_a,
    int obj_b,
    int obj_c,
    int mod_ctx_obj,
    bool append_psync)
{
    std::ostringstream asm_code;
    asm_code << "void hpu_mm_complete(void) {\n";

    asm_code << "    __asm__ volatile(\n";
    asm_code << hpu::dload(mod_ctx_obj, hpu::DataType::mod_ctx,
                           hpu::DloadFlag::small_bank);
    asm_code << hpu::pmodld(0);
    asm_code << hpu::dload(obj_b, hpu::DataType::poly);
    asm_code << hpu::dload(obj_c, hpu::DataType::poly);
    asm_code << generate_hpu_mm_body_asm(obj_a, obj_b, obj_c);
    if (obj_b != obj_a) {
        asm_code << hpu::pfree(obj_b);
    }
    if (obj_c != obj_a && obj_c != obj_b) {
        asm_code << hpu::pfree(obj_c);
    }
    asm_code << hpu::dstore(obj_a, 1);
    asm_code << hpu::pfree(mod_ctx_obj);

    if (append_psync) {
        asm_code << hpu::psync();
    }

    asm_code << "        : \n";
    asm_code << "        : \n";
    asm_code << "        : \"memory\"\n";
    asm_code << "    );\n";
    asm_code << "}\n";

    return asm_code.str();
}
