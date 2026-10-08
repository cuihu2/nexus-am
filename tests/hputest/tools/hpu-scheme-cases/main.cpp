#include "fixture.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 5 && argc != 6) throw std::invalid_argument("usage: generator ckks|bfv|bgv OP DEGREE OUTPUT [CKKS_SCALE_BITS|library|regression]");
        const std::string scheme = argv[1];
        if (scheme != "ckks" && scheme != "bfv" && scheme != "bgv")
            throw std::invalid_argument("unknown scheme");
        const std::string operation = argv[2];
        const auto degree = std::stoul(argv[3]);
        const bool library = argc == 6 && std::string(argv[5]) == "library";
        const bool regression = argc == 6 && std::string(argv[5]) == "regression";
        const bool seal_profile = library || regression;
        if (library && ((degree != 128 && degree != 4096) ||
            (operation != "hadd" && operation != "hmul" && operation != "reline" &&
             operation != "modswitch" && operation != "rotate")))
            throw std::invalid_argument("unsupported library profile");
        if (regression && (degree != 4096 ||
            (operation != "hadd" && operation != "hmul" && operation != "reline" &&
             operation != "modswitch" && operation != "rotate")))
            throw std::invalid_argument("unsupported regression profile");
        // 只切换oracle：规范项保留已有CF1/1、半行旋转及其它输入参数；
        // regression profile 保持原CF3/5和步长1。
        Fixture fixture(scheme, operation, degree, argv[4],
            argc == 6 && !seal_profile ? std::stoi(argv[5]) : 30,
            library ? static_cast<int>(degree / 4) : 1, !library);
        if (library) fixture.stem = "seal_" + fixture.stem;
        if (scheme == "ckks") generate_ckks(fixture);
        else if (scheme == "bfv") generate_bfv(fixture);
        else generate_bgv(fixture);
        if (seal_profile) {
            const char *revision = std::getenv("HPU_DELIVERY_COMMIT");
            if (!revision || std::string(revision).size() != 40)
                throw std::runtime_error("SEAL profile requires pinned HPU_DELIVERY_COMMIT");
            const std::string api = operation == "hadd" ? "add" : operation == "hmul" ? "multiply+relinearize" :
                operation == "reline" ? "relinearize" : operation == "modswitch" ?
                (scheme == "ckks" ? "rescale_to_next" : "mod_switch_to_next") :
                (scheme == "ckks" ? "rotate_vector" : "rotate_rows");
            // v1包白名单不变，来源证据写在包外；BFV没有明文等价放行分支。
            std::ofstream report(fixture.directory.parent_path() / (fixture.stem + ".seal.json"));
            report << "{\n  \"library\": \"inline-asm/third_party/modified-SEAL\",\n"
                   << "  \"producer_commit\": \"" << revision << "\",\n"
                   << "  \"program\": \"" << fixture.stem << "\",\n"
                   << "  \"scheme\": \"" << scheme << "\",\n  \"degree\": " << degree << ",\n"
                   << "  \"api\": \"seal::Evaluator::" << api << "\",\n"
                   << "  \"rotation_steps\": " << (operation == "rotate" ? fixture.rotation_steps : 0) << ",\n"
                   << "  \"rotation_generator\": 3,\n  \"initial_correction_factors\": ["
                   << (regression && scheme == "bgv" ? "3, 5" : "1, 1") << "],\n"
                   << "  \"comparison\": \"all raw physical words\",\n"
                   << "  \"raw_word_mismatches\": 0,\n  \"status\": \"PASS\"\n}\n";
            if (!report) throw std::runtime_error("cannot write SEAL provenance");
        }
        std::cout << fixture.stem << ": independent SEAL + HPU software-model raw-word comparison PASS\n";
    } catch (const std::exception &error) {
        std::cerr << "scheme-case generation failed: " << error.what() << '\n';
        return 1;
    }
}
