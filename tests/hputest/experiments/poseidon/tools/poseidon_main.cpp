#include "poseidon_bridge.hpp"
#include "fixture.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 5) throw std::invalid_argument("usage: generator ckks|bfv|bgv OP 128|4096 OUTPUT");
        const std::string scheme = argv[1], operation = argv[2];
        const auto degree = std::stoul(argv[3]);
        if ((scheme != "ckks" && scheme != "bfv" && scheme != "bgv") ||
            (degree != 128 && degree != 4096) ||
            (operation != "hadd" && operation != "hmul" && operation != "reline" &&
             operation != "modswitch" && operation != "rotate"))
            throw std::invalid_argument("unsupported Poseidon test parameters");
        // 两库的槽位generator不同，半行旋转的Galois元素均为N+1。
        Fixture fixture(scheme, operation, degree, argv[4], 30, static_cast<int>(degree / 4), false);
        const auto oracle = verify_poseidon(fixture);
        fixture.stem = "poseidon_" + fixture.stem;
        if (scheme == "ckks") generate_ckks(fixture);
        else if (scheme == "bfv") generate_bfv(fixture);
        else generate_bgv(fixture);
        // 真实库调用及对应验收均成功才写PASS；BFV乘法的语义差异明确记录。
        // v1包有严格文件白名单；Poseidon证据放在包外，不能破坏原schema。
        std::ofstream report(fixture.directory.parent_path() / (fixture.stem + ".poseidon.json"));
        report << "{\n  \"library\": \"https://github.com/luhang-HPU/poseidon\",\n"
               << "  \"revision\": \"" << POSEIDON_REVISION << "\",\n"
               << "  \"scheme\": \"" << scheme << "\",\n"
               << "  \"degree\": " << degree << ",\n"
               << "  \"program\": \"" << fixture.stem << "\",\n"
               << "  \"api\": \"" << oracle.api << "\",\n"
               << "  \"rotation_steps\": " << (operation == "rotate" ? fixture.rotation_steps : 0) << ",\n"
               << "  \"device\": \"software\",\n  \"key_switch\": \"BV/P=1\",\n"
               << "  \"raw_word_mismatches\": " << oracle.raw_word_mismatches << ",\n"
               << "  \"plaintext_coefficients_compared\": " << oracle.plaintext_coefficients_compared << ",\n"
               << "  \"comparison\": \"" <<
                  (scheme == "bfv" && operation == "hmul" ?
                   "exact decrypted BFV polynomial; SEAL physical golden" :
                   "all raw words and ciphertext metadata") << "\",\n"
               << "  \"status\": \"PASS\"\n}\n";
        if (!report) throw std::runtime_error("cannot write Poseidon oracle provenance");
        std::cout << fixture.stem << ": Poseidon API + SEAL "
                  << (scheme == "bfv" && operation == "hmul" ? "exact decrypted polynomial" : "raw-word")
                  << " comparison PASS\n";
    } catch (const std::exception &error) {
        std::cerr << "Poseidon-case generation failed: " << error.what() << '\n';
        return 1;
    }
}
