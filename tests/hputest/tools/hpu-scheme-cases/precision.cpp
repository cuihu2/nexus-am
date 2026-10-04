#include "fixture.hpp"
#include "hpu/seal/ntt_bridge.hpp"
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>

// 从同一个确定性 SEAL fixture 重建私钥，只在主机评估解密误差，不交付私钥。
int main(int argc,char **argv) {
    try {
        if (argc!=5) throw std::runtime_error("usage: precision RESULT_IMAGE FIRST_OUTPUT_LINE DEGREE SCALE_BITS");
        const auto n=std::stoul(argv[3]);
        const auto bits=std::stoi(argv[4]);
        Fixture fixture("ckks","reline",n,{},bits);
        const auto data=fixture.context->get_context_data(fixture.expected.parms_id());
        const auto &moduli=data->parms().coeff_modulus();
        const auto &key_moduli=fixture.context->key_context_data()->parms().coeff_modulus();
        std::ifstream image(argv[1],std::ios::binary);
        if (!image) throw std::runtime_error("missing image");
        seal::Ciphertext actual;
        actual.resize(*fixture.context,fixture.expected.parms_id(),fixture.expected.size());
        actual.is_ntt_form()=true; actual.scale()=fixture.expected.scale();
        std::size_t raw_mismatches=0;
        for (std::size_t c=0;c<actual.size();++c) {
            hpu::seal_adapter::HpuRnsPolynomial physical;
            physical.degree=n;
            for (const auto &q:moduli) {
                physical.moduli.push_back(static_cast<std::uint32_t>(q.value()));
                const auto found=std::find_if(key_moduli.begin(),key_moduli.end(),
                    [&](const auto &key){return key.value()==q.value();});
                physical.modulus_ids.push_back(static_cast<std::uint8_t>(found-key_moduli.begin()));
            }
            physical.words.resize(n*moduli.size());
            image.seekg(std::stoull(argv[2])*256+c*n*moduli.size()*4);
            image.read(reinterpret_cast<char*>(physical.words.data()),physical.words.size()*4);
            if (!image) throw std::runtime_error("truncated result");
            const auto expected=hpu::seal_adapter::ciphertext_component_to_hpu(fixture.expected,c,*fixture.context);
            for (std::size_t i=0;i<physical.words.size();++i)
                raw_mismatches+=physical.words[i]!=expected.words[i];
            const auto imported=hpu::seal_adapter::hpu_to_seal_ntt(physical,actual.parms_id(),*fixture.context);
            std::copy(imported.begin(),imported.end(),actual.data(c));
        }
        seal::Decryptor decryptor(*fixture.context,fixture.keys->secret_key());
        seal::CKKSEncoder encoder(*fixture.context);
        seal::Plaintext decoded_actual,decoded_expected;
        decryptor.decrypt(actual,decoded_actual); decryptor.decrypt(fixture.expected,decoded_expected);
        std::vector<std::complex<double>> a,b;
        encoder.decode(decoded_actual,a); encoder.decode(decoded_expected,b);
        double difference=0,native_error=0,seal_error=0;
        const std::vector<double> expected_slots{0.5,-0.75,-2.5,1.125};
        for (std::size_t i=0;i<a.size();++i) {
            const double message=i<expected_slots.size()?expected_slots[i]:0.0;
            difference=std::max(difference,std::abs(a[i]-b[i]));
            native_error=std::max(native_error,std::abs(a[i]-message));
            seal_error=std::max(seal_error,std::abs(b[i]-message));
        }
        std::cout<<std::setprecision(17)<<"{\"degree\":"<<n<<",\"input_scale_bits\":"<<bits
                 <<",\"output_scale\":"<<actual.scale()<<",\"raw_word_mismatches\":"<<raw_mismatches
                 <<",\"max_native_to_seal_decoded_error\":"<<difference
                 <<",\"max_native_to_message_error\":"<<native_error
                 <<",\"max_seal_to_message_error\":"<<seal_error<<"}\n";
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
