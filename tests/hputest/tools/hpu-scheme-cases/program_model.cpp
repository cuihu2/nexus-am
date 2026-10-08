// 顺序执行真实 producer ASM：检查生命周期、DMA、模上下文和每条算术/NTT。
// 此工具只是一项额外主机检查；不会把结果当作 SEAL golden 或 RTL 通过证据。
#include "assembler.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Words = std::vector<std::uint32_t>;
using Row = std::map<std::string, std::string>;

static std::vector<std::string> fields(const std::string &line) {
    std::vector<std::string> result;
    std::string field;
    bool quoted = false;
    for (std::size_t i=0; i<line.size(); ++i) {
        const char c=line[i];
        if (c=='"') {
            if (quoted && i+1<line.size() && line[i+1]=='"') { field+='"'; ++i; }
            else quoted=!quoted;
        } else if (c==',' && !quoted) { result.push_back(field); field.clear(); }
        else if (c!='\r') field+=c;
    }
    result.push_back(field);
    return result;
}

static std::vector<Row> csv(const std::string &path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("missing CSV: "+path);
    std::string line;
    std::getline(stream,line);
    const auto headers=fields(line);
    std::vector<Row> result;
    while (std::getline(stream,line)) {
        const auto values=fields(line);
        if (values.size()!=headers.size()) throw std::runtime_error("CSV shape");
        Row row;
        for (std::size_t i=0;i<values.size();++i) row[headers[i]]=values[i];
        result.push_back(std::move(row));
    }
    return result;
}

static Words binary(const std::string &path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if (!stream) throw std::runtime_error("missing binary: "+path);
    const auto size=stream.tellg();
    if (size<0 || size%4) throw std::runtime_error("uint32 shape");
    Words words(static_cast<std::size_t>(size)/4);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(words.data()),size);
    if (!stream) throw std::runtime_error("truncated binary");
    return words;
}

static void stage(Words &values,const Words &twiddles,unsigned stage_id,
                  std::uint32_t q,bool inverse) {
    const std::size_t n=values.size();
    unsigned log=0; for (auto v=n;v>1;v>>=1) ++log;
    if (n<128 || (n&(n-1)) || stage_id>=log || twiddles.size()!=n/2)
        throw std::runtime_error("STG length/stage mismatch");
    const auto m=std::size_t{1}<<(inverse?log-1-stage_id:stage_id);
    std::size_t tw=0;
    auto batch=[&](std::size_t first,std::size_t second,bool interleaved) {
        std::array<std::size_t,128> positions{};
        std::array<std::uint32_t,128> v{},permuted{};
        for (std::size_t j=0;j<128;++j) {
            positions[j]=interleaved?(j%2?second+j/2:first+j/2):first+j;
            v[j]=values[positions[j]];
        }
        if (inverse) {
            for (std::size_t j=0;j<128;++j) permuted[((j<<1)&127)|(j>>6)]=v[j];
            v=permuted;
        }
        for (unsigned lane=0;lane<64;++lane) {
            const std::uint64_t a=v[2*lane];
            const std::uint64_t b=std::uint64_t(v[2*lane+1])*twiddles[tw++]%q;
            v[2*lane]=static_cast<std::uint32_t>((a+b)%q);
            v[2*lane+1]=static_cast<std::uint32_t>((a+q-b)%q);
        }
        if (!inverse) {
            for (std::size_t j=0;j<128;++j) permuted[(j>>1)|((j&1)<<6)]=v[j];
            v=permuted;
        }
        for (std::size_t j=0;j<128;++j) values[positions[j]]=v[j];
    };
    if (m<128) for (std::size_t first=0;first<n;first+=128) batch(first,first+64,false);
    else for (std::size_t group=0;group<n;group+=2*m)
        for (std::size_t offset=0;offset<m;offset+=64) batch(group+offset,group+m+offset,true);
}

int main(int argc,char **argv) {
    std::size_t pc=0;
    std::string instruction;
    try {
        if (argc!=7) throw std::runtime_error("usage: model ASM IMAGE DMA GOLDEN_MANIFEST PACKAGE_ROOT RESULT");
        std::ifstream source(argv[1]);
        if (!source) throw std::runtime_error("missing ASM");
        const std::string text((std::istreambuf_iterator<char>(source)),{});
        const std::regex quoted("\"([^\"]*)\\\\n\\\\t\"");
        std::string assembly;
        for (auto it=std::sregex_iterator(text.begin(),text.end(),quoted);it!=std::sregex_iterator();++it)
            assembly+=(*it)[1].str()+"\n";
        if (assembly.empty()) assembly=text; // v1 包使用纯汇编；legacy 为 C 字符串片段。
        const auto encoded=hpu::assemble_source(assembly);
        if (encoded.empty()) throw std::runtime_error("empty program");
        const auto dma=csv(argv[3]);
        auto memory=binary(argv[2]);
        std::array<Words,8> objects;
        std::array<bool,8> live{};
        Words modulus_table;
        std::uint32_t q=0;
        std::size_t dma_index=0;
        auto require_live=[&](int object) {
            if (object<0 || object>7 || !live[object])
                throw std::runtime_error("use/free of non-live p"+std::to_string(object));
        };
        for (;pc<encoded.size();++pc) {
            const auto &i=encoded[pc].instruction;
            instruction=encoded[pc].normalized_asm;
            using M=hpu::Mnemonic;
            if (i.mnemonic==M::kDload || i.mnemonic==M::kDstore) {
                if (dma_index>=dma.size()) throw std::runtime_error("missing DMA span");
                const auto &row=dma[dma_index++];
                if (std::stoul(row.at("instruction_index"))!=pc) throw std::runtime_error("DMA PC mismatch");
                const auto first=std::stoull(row.at("line_offset"))*64;
                const auto count=std::stoull(row.at("line_count"))*64;
                if (!count || first+count>memory.size()) throw std::runtime_error("DMA bounds");
                const auto object=i.obj_id;
                if (i.mnemonic==M::kDload) {
                    if (live[object]) throw std::runtime_error("DLOAD overwrites live object");
                    objects[object]=Words(memory.begin()+first,memory.begin()+first+count);
                    live[object]=true;
                    if (i.type==2) modulus_table=objects[object];
                } else {
                    require_live(object);
                    if (objects[object].size()!=count) throw std::runtime_error("DSTORE OBJ.len mismatch");
                    std::copy(objects[object].begin(),objects[object].end(),memory.begin()+first);
                    live[object]=false; // 当前 RTL：rel=0 和 1 均释放。
                }
            } else if (i.mnemonic==M::kPfree) {
                require_live(i.idx0); live[i.idx0]=false;
            } else if (i.mnemonic==M::kPmodld) {
                const auto word=std::size_t(i.mod_id)*4;
                if (word+4>modulus_table.size()) throw std::runtime_error("MOD_ID bounds");
                q=modulus_table[word];
                if (q<65537) throw std::runtime_error("invalid q");
            } else if (i.mnemonic==M::kPsync) {
                if (pc+1!=encoded.size()) throw std::runtime_error("nonterminal PSYNC");
            } else if (i.mnemonic==M::kPntt || i.mnemonic==M::kPintt) {
                require_live(i.pdst); require_live(i.psrc1);
                if (!q) throw std::runtime_error("missing modulus");
                stage(objects[i.pdst],objects[i.psrc1],i.idx0,q,i.mnemonic==M::kPintt);
            } else {
                require_live(i.psrc1);
                if (i.imm8<0) require_live(i.psrc2);
                if (i.mnemonic==M::kPmac) require_live(i.pdst);
                if (!q) throw std::runtime_error("missing modulus");
                const auto &left=objects[i.psrc1];
                if (i.imm8<0 && objects[i.psrc2].size()!=left.size()) throw std::runtime_error("AR3 source length");
                if (live[i.pdst] && objects[i.pdst].size()!=left.size()) throw std::runtime_error("AR3 destination length");
                Words output(left.size());
                for (std::size_t word=0;word<output.size();++word) {
                    const std::uint64_t a=left[word];
                    const std::uint64_t b=i.imm8>=0?std::uint32_t(i.imm8):objects[i.psrc2][word];
                    std::uint64_t value;
                    if (i.mnemonic==M::kPadd) value=(a+b)%q;
                    else if (i.mnemonic==M::kPsub) value=(a+q-b)%q;
                    else {
                        value=a*b%q;
                        if (i.mnemonic==M::kPmac) value=(value+objects[i.pdst][word])%q;
                    }
                    output[word]=static_cast<std::uint32_t>(value);
                }
                objects[i.pdst]=std::move(output); live[i.pdst]=true;
            }
        }
        if (dma_index!=dma.size()) throw std::runtime_error("extra DMA spans");
        for (std::size_t object=0; object<live.size(); ++object) {
            if (live[object])
                throw std::runtime_error("live object at program end p"+std::to_string(object));
        }
        unsigned failures=0;
        for (const auto &row:csv(argv[4])) {
            const auto expected=binary(std::string(argv[5])+"/"+row.at("path"));
            const auto first=std::stoull(row.at("line_offset"))*64;
            if (first+expected.size()>memory.size()) throw std::runtime_error("golden bounds");
            auto mismatch=std::mismatch(expected.begin(),expected.end(),memory.begin()+first);
            if (mismatch.first!=expected.end()) {
                const auto index=mismatch.first-expected.begin();
                std::cerr<<"PROGRAM_MODEL_MISMATCH object="<<row.at("object_id")
                         <<" component="<<row.at("component")<<" mod="<<row.at("modulus_id")
                         <<" word="<<index<<" actual="<<*mismatch.second
                         <<" seal="<<*mismatch.first<<'\n';
                ++failures;
            }
        }
        std::ofstream result(argv[6],std::ios::binary);
        result.write(reinterpret_cast<const char*>(memory.data()),memory.size()*4);
        std::cout<<"PROGRAM_MODEL commands="<<pc<<" dma="<<dma_index<<" mismatched_limbs="<<failures<<'\n';
        return failures?1:0;
    } catch (const std::exception &error) {
        std::cerr<<"PROGRAM_MODEL_FAIL pc="<<pc<<" asm="<<instruction<<" reason="<<error.what()<<'\n';
        return 2;
    }
}
