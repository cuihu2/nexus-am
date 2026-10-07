# 05组：inline-asm/modified-SEAL算法库验证

当前默认参考库为inline-asm main固定提交`c11dfe2cfbe731d7b45aea0a62d993d733e1a089`
内的`third_party/modified-SEAL`。通过其`SEAL::seal`与HPU适配层构建，
不使用系统安装的标准SEAL，也不调用Poseidon。库版本随inline-asm gitlink冻结。

## 层级与用例

04仍只放BConv、整体NTT/INTT、KeySwitch、Auto和指令链；05验收完整库操作。
KeySwitch隔离c2切换，Reline保留并合并完整密文c0/c1，不能按共享指令链合并。

05共39项：30项SEAL规范用例、7项不同参数回归、2项待接入记录。

| 操作 | CKKS | BFV/BGV |
|---|---|---|
| HADD | `Evaluator::add` | `Evaluator::add` |
| HMUL_RELINE | `multiply`后`relinearize` | `multiply`后`relinearize` |
| RELINE | `relinearize` | `relinearize` |
| RESCALE/MODSWITCH | `rescale_to_next` | `mod_switch_to_next` |
| ROTATE | `rotate_vector` | `rotate_rows` |

三方案各提供N128/N4096，文件分别在`src/05_algorithm_library/01_ckks`、
`02_bfv`、`03_bgv`，一个C文件一个ELF，只执行一遍。
编码器、Encryptor、评估密钥、Evaluator与golden都使用同一份modified-SEAL。

本轮只切换参考依赖，不同时更换输入：规范项保留CF1/1和半行旋转，
旋转步长为N/4（N128为32，N4096为1024），SEAL generator为3。
`04_parameter_regression`保留BGV CF3/5、CF15、CF3及三方案步长1旋转，
不会把参数不同的测试当成完全重复项。
`05_pending_interfaces`只记录Encode/Bootstrap接收缺口，不发布占位ELF。

## 严格参考与目标检查

BFV也以modified-SEAL输出的原始密文为golden，**不允许以“解密明文相同”放行原始word失配**。
Poseidon与该fork的BFV乘法曾产生不同噪声密文，该差异仅保留在实验记录，
不进入当前默认验收。

规范profile由`hpu_scheme_case_generator ... library`生成，
sidecar `*.seal.json`记录库来源、生产者提交、实际API、方案、N、CF、旋转步长与逐word检查状态。
sidecar位于原包外，不改变上游v1白名单。接收后写为`SEAL_ORACLE.json`，
进入`AM_ADAPTATION.json`的`seal_oracle`字段。

上游HPU软件执行器必须逐word匹配modified-SEAL；AM继续复放实际ASM、DMA与对象生命周期，
保留既有BFV物理排列、CKKS rounded-P和输入shadow适配。
目标检查完整输出、只读区和guard，BFV/CKKS/BGV均返回0才是该次自检通过。
每个程序DSTORE后唯一PSYNC，通过MMIO等待完成且BUSY清零，不启用PLIC/timer。

软件模型通过、构建通过不等于IT/VCS通过。N128仅作快速功能定位，参数不作为安全部署承诺。

## Poseidon单独保留

原30项Poseidon测试、桥接工具、参考检查和固定SDK在
`experiments/poseidon/`。它们不在默认`cases.tsv`/`scheme-cases.tsv`中，
默认CMake、make和Actions不构建、不下载该SDK、不发布其实验ELF。
未删除Poseidon源码，也不改写其SDK；日后对照需显式运行实验目录的独立入口。

旧编号仍能在`case-aliases.tsv`中找到当前规范项，
04/05层级迁移见`layer-migration.tsv`。本次不会重引入已合并的8个重复程序。

## 默认构建

```sh
git submodule update --init --recursive \
  tests/hputest/third_party/inline-asm \
  tests/hputest/third_party/hpu-seal \
  tests/hputest/third_party/hpu-applications
make -C tests/hputest all JOBS=4
make -C tests/hputest silent JOBS=4
```

无需GMP/Poseidon。沿用普通摘要与真正静默两种版本；
顶层MANIFEST记录`golden_backend=inline-asm/modified-SEAL`及inline-asm版本。
生成的数据、ELF、BIN和反汇编只进入Actions artifact，不提交到源码仓库。
