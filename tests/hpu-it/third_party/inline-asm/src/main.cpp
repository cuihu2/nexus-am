#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "util/bconv.hpp"
#include "util/mm.hpp"
#include "util/ntt.hpp"
#include "poly/auto.hpp"
#include "poly/cmult.hpp"
#include "poly/moddown.hpp"
#include "poly/modup.hpp"
#include "poly/pmult.hpp"
#include "operator/keyswitch.hpp"
#include "operator/relinearization.hpp"
#include "operator/ciphertext_multiply.hpp"
#include "operator/encode.hpp"
#include "operator/rescale.hpp"

namespace {

enum class OutputMode {
	CPP,
	ASM,
	BOTH
};

OutputMode g_output_mode = OutputMode::BOTH;

struct NttConfig {
	int N;
	int obj_poly;
	int twiddle_obj;
	int mod_ctx_obj;
};

struct MmConfig {
	int obj_a;
	int obj_b;
	int obj_c;
	int mod_ctx_obj;
};

struct BconvConfig {
	int num_q;
	int num_p;
	int obj_q_base;
	int obj_tmp_base;
	int obj_p_base;
	int obj_qhat_inv_base;
	int obj_qhat_modp_base;
	int mod_ctx_q_base;
	int mod_ctx_p_base;
};

struct PmultConfig {
	int num_q;
	int ct0_base;
	int ct1_base;
	int pt_base;
	int out0_base;
	int out1_base;
	int mod_ctx_q_base;
};

struct CmultConfig {
	int num_q;
	int a0_base;
	int a1_base;
	int b0_base;
	int b1_base;
	int out0_base;
	int out1_base;
	int out2_base;
	int mod_ctx_q_base;
};

struct ModdownConfig {
	int num_q;
	int num_p;
	int q_base;
	int p_base;
	int tmp_base;
	int qcorr_base;
	int phat_inv_base;
	int phat_modq_base;
	int mod_ctx_p_base;
	int mod_ctx_q_base;
};

struct AutoConfig {
	int N;
	int num_q;
	int num_p;
	int dnum;
	int auto_idx;
};

struct CiphertextMultiplyConfig {
	int N;
	int num_q;
	int num_p;
	int dnum;
};

struct EncodeConfig {
	int N;
	int num_q;
};

struct RescaleConfig {
	int num_q;
	int num_components;
};

constexpr NttConfig kNttCfg{4096, 0, 1, 2};
constexpr MmConfig kMmCfg{0, 1, 2, 3};
// 为了缩短独立 BConv 示例，采用 num_q = num_p = 1；模上下文使用独立 8-bit MOD_ID
constexpr BconvConfig kBconvCfg{1, 1, 0, 1, 2, 3, 4, 5, 6};
constexpr PmultConfig kPmultCfg{4, 0, 1, 2, 3, 4, 5};
constexpr CmultConfig kCmultCfg{4, 0, 1, 2, 3, 4, 5, 6, 7};
constexpr ModdownConfig kModdownCfg{4, 3, 0, 1, 2, 3, 4, 5, 6, 7};
constexpr AutoConfig kAutoCfg{4096, 4, 3, 2, 1};
constexpr CiphertextMultiplyConfig kCiphertextMultiplyCfg{4096, 4, 3, 2};
constexpr EncodeConfig kEncodeCfg{4096, 4};
constexpr RescaleConfig kRescaleCfg{4, 2};

void test_encode_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::ofstream("output/encode.cpp")
			<< generate_hpu_encode_asm(kEncodeCfg.N, kEncodeCfg.num_q, true);
		std::cout << "Saved encode ASM to output/encode.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::ofstream("output/encode.asm")
			<< generate_hpu_encode_body_asm(kEncodeCfg.N, kEncodeCfg.num_q, true);
		std::cout << "Saved encode body ASM to output/encode.asm\n";
	}
}

void test_rescale_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::ofstream("output/rescale.cpp")
			<< generate_hpu_rescale_asm(
				kRescaleCfg.num_q, kRescaleCfg.num_components, true);
		std::cout << "Saved rescale ASM to output/rescale.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::ofstream("output/rescale.asm")
			<< generate_hpu_rescale_body_asm(
				kRescaleCfg.num_q, kRescaleCfg.num_components, true);
		std::cout << "Saved rescale body ASM to output/rescale.asm\n";
	}
}

void test_intt_codegen() {
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string intt = generate_hpu_intt_asm(
		kNttCfg.N,
		kNttCfg.obj_poly,
		kNttCfg.twiddle_obj,
		kNttCfg.mod_ctx_obj,
		true);
	std::ofstream("output/intt.cpp") << intt;
	std::cout << "Saved intt ASM to output/intt.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string intt_body = generate_hpu_intt_asm(
		kNttCfg.N,
		kNttCfg.obj_poly,
		kNttCfg.twiddle_obj,
		kNttCfg.mod_ctx_obj,
		true);
	std::ofstream("output/intt.asm") << intt_body;
	std::cout << "Saved intt body ASM to output/intt.asm\n";
	}
}

void test_ntt_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string ntt = generate_hpu_ntt_asm(
		kNttCfg.N,
		kNttCfg.obj_poly,
		kNttCfg.twiddle_obj,
		kNttCfg.mod_ctx_obj,
		true);
	std::ofstream("output/ntt.cpp") << ntt;
	std::cout << "Saved ntt ASM to output/ntt.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string ntt_body = generate_hpu_ntt_asm(
		kNttCfg.N,
		kNttCfg.obj_poly,
		kNttCfg.twiddle_obj,
		kNttCfg.mod_ctx_obj,
		true);
	std::ofstream("output/ntt.asm") << ntt_body;
	std::cout << "Saved ntt body ASM to output/ntt.asm\n";
	}
}

void test_mm_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string mm = generate_hpu_mm_asm(
		kMmCfg.obj_a,
		kMmCfg.obj_b,
		kMmCfg.obj_c,
		kMmCfg.mod_ctx_obj,
		true);
	std::ofstream("output/mm.cpp") << mm;
	std::cout << "Saved mm ASM to output/mm.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string mm_body = generate_hpu_mm_asm(
		kMmCfg.obj_a,
		kMmCfg.obj_b,
		kMmCfg.obj_c,
		kMmCfg.mod_ctx_obj,
		true);
	std::ofstream("output/mm.asm") << mm_body;
	std::cout << "Saved mm body ASM to output/mm.asm\n";
	}
}

void test_bconv_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string bconv = generate_hpu_bconv_asm(
		kBconvCfg.num_q,
		kBconvCfg.num_p,
		0,
		true);
	std::ofstream("output/bconv.cpp") << bconv;
	std::cout << "Saved bconv ASM to output/bconv.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string bconv_body = generate_hpu_bconv_body_asm(
		kBconvCfg.num_q,
		kBconvCfg.num_p,
		0,
		true);
	std::ofstream("output/bconv.asm") << bconv_body;
	std::cout << "Saved bconv body ASM to output/bconv.asm\n";
	}
}

void test_pmult_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string pmult = generate_hpu_pmult_asm(
		kPmultCfg.num_q,
		true);
	std::ofstream("output/pmult.cpp") << pmult;
	std::cout << "Saved pmult ASM to output/pmult.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string pmult_body = generate_hpu_pmult_body_asm(
		kPmultCfg.num_q,
		true);
	std::ofstream("output/pmult.asm") << pmult_body;
	std::cout << "Saved pmult body ASM to output/pmult.asm\n";
	}
}

void test_cmult_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string cmult = generate_hpu_cmult_asm(
		kCmultCfg.num_q,
		true);
	std::ofstream("output/cmult.cpp") << cmult;
	std::cout << "Saved cmult ASM to output/cmult.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string cmult_body = generate_hpu_cmult_body_asm(
		kCmultCfg.num_q,
		true);
	std::ofstream("output/cmult.asm") << cmult_body;
	std::cout << "Saved cmult body ASM to output/cmult.asm\n";
	}
}

void test_modup_codegen()
{
		if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
			std::string modup = generate_hpu_modup_asm(
				kCiphertextMultiplyCfg.num_q,
				kCiphertextMultiplyCfg.num_p,
				kCiphertextMultiplyCfg.num_q / kCiphertextMultiplyCfg.dnum,
				0,
				true);
	std::ofstream("output/modup.cpp") << modup;
	std::cout << "Saved modup ASM to output/modup.cpp\n";
	}

		if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
			std::string modup_body = generate_hpu_modup_body_asm(
				kCiphertextMultiplyCfg.num_q,
				kCiphertextMultiplyCfg.num_p,
				kCiphertextMultiplyCfg.num_q / kCiphertextMultiplyCfg.dnum,
				0,
				true);
	std::ofstream("output/modup.asm") << modup_body;
	std::cout << "Saved modup body ASM to output/modup.asm\n";
	}
}

void test_auto_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string auto_code = generate_hpu_auto_asm(
		kAutoCfg.N,
		kAutoCfg.num_q,
		kAutoCfg.num_p,
		kAutoCfg.dnum,
		kAutoCfg.auto_idx,
		true);
	std::ofstream("output/auto.cpp") << auto_code;
	std::cout << "Saved auto ASM to output/auto.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string auto_body = generate_hpu_auto_body_asm(
		kAutoCfg.N,
		kAutoCfg.num_q,
		kAutoCfg.num_p,
		kAutoCfg.dnum,
		kAutoCfg.auto_idx,
		true);
	std::ofstream("output/auto.asm") << auto_body;
	std::cout << "Saved auto body ASM to output/auto.asm\n";
	}
}

void test_moddown_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string moddown = generate_hpu_moddown_asm(
		kModdownCfg.num_q,
		kModdownCfg.num_p,
		true);
	std::ofstream("output/moddown.cpp") << moddown;
	std::cout << "Saved moddown ASM to output/moddown.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string moddown_body = generate_hpu_moddown_body_asm(
		kModdownCfg.num_q,
		kModdownCfg.num_p,
		true);
	std::ofstream("output/moddown.asm") << moddown_body;
	std::cout << "Saved moddown body ASM to output/moddown.asm\n";
	}
}

void test_keyswitch_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string keyswitch = generate_hpu_keyswitch_asm(
			kCiphertextMultiplyCfg.N,
			kCiphertextMultiplyCfg.num_q,
			kCiphertextMultiplyCfg.num_p,
			kCiphertextMultiplyCfg.dnum,
			true);
	std::ofstream("output/keyswitch.cpp") << keyswitch;
	std::cout << "Saved keyswitch ASM to output/keyswitch.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string keyswitch_body = generate_hpu_keyswitch_body_asm(
			kCiphertextMultiplyCfg.N,
			kCiphertextMultiplyCfg.num_q,
			kCiphertextMultiplyCfg.num_p,
			kCiphertextMultiplyCfg.dnum,
			true);
	std::ofstream("output/keyswitch.asm") << keyswitch_body;
	std::cout << "Saved keyswitch body ASM to output/keyswitch.asm\n";
	}
}

void test_relinearization_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string relinearization = generate_hpu_relinearization_asm(
			kCiphertextMultiplyCfg.N,
			kCiphertextMultiplyCfg.num_q,
			kCiphertextMultiplyCfg.num_p,
			kCiphertextMultiplyCfg.dnum,
			true);
		std::ofstream("output/relinearization.cpp") << relinearization;
		std::cout << "Saved relinearization ASM to output/relinearization.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string relinearization_body = generate_hpu_relinearization_body_asm(
			kCiphertextMultiplyCfg.N,
			kCiphertextMultiplyCfg.num_q,
			kCiphertextMultiplyCfg.num_p,
			kCiphertextMultiplyCfg.dnum,
			true);
		std::ofstream("output/relinearization.asm") << relinearization_body;
		std::cout << "Saved relinearization body ASM to output/relinearization.asm\n";
	}
}

void test_ciphertext_multiply_codegen()
{
	if (g_output_mode == OutputMode::CPP || g_output_mode == OutputMode::BOTH) {
		std::string ciphertext_multiply = generate_hpu_ciphertext_multiply_asm(
		kCiphertextMultiplyCfg.N,
		kCiphertextMultiplyCfg.num_q,
		kCiphertextMultiplyCfg.num_p,
		kCiphertextMultiplyCfg.dnum,
		true);
	std::ofstream("output/ciphertext_multiply.cpp") << ciphertext_multiply;
	std::cout << "Saved ciphertext_multiply ASM to output/ciphertext_multiply.cpp\n";
	}

	if (g_output_mode == OutputMode::ASM || g_output_mode == OutputMode::BOTH) {
		std::string ciphertext_multiply_body = generate_hpu_ciphertext_multiply_body_asm(
		kCiphertextMultiplyCfg.N,
		kCiphertextMultiplyCfg.num_q,
		kCiphertextMultiplyCfg.num_p,
		kCiphertextMultiplyCfg.dnum,
		true);
	std::ofstream("output/ciphertext_multiply.asm") << ciphertext_multiply_body;
	std::cout << "Saved ciphertext_multiply body ASM to output/ciphertext_multiply.asm\n";
	}
}

} // namespace

int main(int argc, char* argv[])
{
	std::string mode = "both";
	if (argc > 1) {
		mode = argv[1];
	}
	
	if (mode == "cpp") {
		g_output_mode = OutputMode::CPP;
	} else if (mode == "asm") {
		g_output_mode = OutputMode::ASM;
	} else {
		g_output_mode = OutputMode::BOTH;
	}

	std::filesystem::create_directory("output");
	test_ntt_codegen();
	test_intt_codegen();
	test_encode_codegen();
	test_rescale_codegen();
	test_mm_codegen();
	test_bconv_codegen();
	test_pmult_codegen();
	test_cmult_codegen();
	test_modup_codegen();
	test_moddown_codegen();
	test_auto_codegen();
	test_keyswitch_codegen();
	test_relinearization_codegen();
	test_ciphertext_multiply_codegen();
	return 0;
}
