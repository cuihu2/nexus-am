#include "fixture.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 5 && argc != 6) throw std::invalid_argument("usage: generator ckks|bfv|bgv OP DEGREE OUTPUT [CKKS_SCALE_BITS]");
        const std::string scheme = argv[1];
        if (scheme != "ckks" && scheme != "bfv" && scheme != "bgv")
            throw std::invalid_argument("unknown scheme");
        Fixture fixture(scheme, argv[2], std::stoul(argv[3]), argv[4], argc == 6 ? std::stoi(argv[5]) : 30);
        if (scheme == "ckks") generate_ckks(fixture);
        else if (scheme == "bfv") generate_bfv(fixture);
        else generate_bgv(fixture);
        std::cout << fixture.stem << ": independent SEAL + HPU software-model raw-word comparison PASS\n";
    } catch (const std::exception &error) {
        std::cerr << "scheme-case generation failed: " << error.what() << '\n';
        return 1;
    }
}
