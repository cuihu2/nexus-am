#define main program_model_main
#include "program_model.cpp"
#undef main
#include "hpu/model/hardware_ntt.hpp"

static std::size_t reversed(std::size_t i,std::size_t n) {
    std::size_t out=0;
    for (;n>1;n>>=1) { out=(out<<1)|(i&1); i>>=1; }
    return out;
}

int main() {
    try {
        for (const std::size_t n : {128U,4096U,65536U}) {
            const std::uint32_t q=2013265921U;
            const auto omega=hpu::model::pow_mod(31,(q-1)/n,q);
            const hpu::model::HardwareNttModel model(n,q,omega);
            Words coefficients(n),physical(n);
            for (std::size_t i=0;i<n;++i) coefficients[i]=(i*37+19)%q;
            for (std::size_t i=0;i<n;++i) physical[i]=coefficients[reversed(i,n)];
            const auto forward=model.forward_twiddles();
            for (unsigned i=0;i<forward.size();++i) stage(physical,forward[i],i,q,false);
            if (physical!=model.forward(coefficients)) throw std::runtime_error("forward stage mismatch");
            const auto inverse=model.inverse_twiddles();
            for (unsigned i=0;i<inverse.stages.size();++i) stage(physical,inverse.stages[i],i,q,true);
            for (std::size_t i=0;i<n;++i)
                if (std::uint64_t(physical[i])*inverse.post_scale[i]%q!=coefficients[reversed(i,n)])
                    throw std::runtime_error("inverse stage mismatch");
            std::cout<<"program stage model matches producer hardware model: N="<<n<<'\n';
        }
    } catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
