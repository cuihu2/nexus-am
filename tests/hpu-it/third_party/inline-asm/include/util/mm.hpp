#pragma once

#include <string>

std::string generate_hpu_mm_body_asm(
    int obj_a,
    int obj_b,
    int obj_c,
    bool append_psync = false);

std::string generate_hpu_mm_asm(
    int obj_a,
    int obj_b,
    int obj_c,
    int mod_ctx_obj,
    bool append_psync = true);
