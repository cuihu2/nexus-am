#pragma once

#include <string>

// Coefficient-domain rounded RNS rescale. The final Q limb is used as the
// divisor and removed from every ciphertext component.
std::string generate_hpu_rescale_body_asm(
    int num_q,
    int num_components,
    bool append_psync = false);

std::string generate_hpu_rescale_asm(
    int num_q,
    int num_components,
    bool append_psync = true);
