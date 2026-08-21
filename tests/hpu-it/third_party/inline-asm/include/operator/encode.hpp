#pragma once

#include <string>

// Hardware Encode boundary:
//   host signed coefficients -> RNS-Q embedding (host preprocessing)
//   RNS-Q coefficient limbs   -> RNS-Q NTT plaintext (this HPU stream)
std::string generate_hpu_encode_body_asm(
    int N,
    int num_q,
    bool append_psync = false);

std::string generate_hpu_encode_asm(
    int N,
    int num_q,
    bool append_psync = true);
