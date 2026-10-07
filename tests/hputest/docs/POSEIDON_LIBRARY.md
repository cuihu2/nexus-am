# 05组：Poseidon算法库接口测试

使用用户指定的[Poseidon文档](https://poseidon-hpu.readthedocs.io/en/latest/)，
对应[官方源码](https://github.com/luhang-HPU/poseidon)。AM以submodule固定main提交
`961df3acfc394b634de3fd846946c851903a0e72`，构建不追踪浮动HEAD。
原05组结构连接测试移至08组，七个用例的内容和ID不变；06/07编号不变。
04现在只做基本/组合算子；完整库接口统一属于05，不能靠参考库名称把同一层重复分组。

## 用例与目录

04保留11项：BConv、整体NTT/INTT、三方案两规模KeySwitch、Auto、1项待接入的指令链异常测试。
05共39项：以下30项Poseidon规范用例，另有7项参数回归和2项待接入记录。

旧04的CKKS/BFV HADD、HMUL、Reline、降层共8项，与05对应项的输入、golden、
可写掩码和INST32完全一致，已合并。旧ID直接映射到当前ID，不发布第二份ELF。
移除的源码可从提交`e296df09`恢复；机器可读映射见[`layer-migration.tsv`](../layer-migration.tsv)。

`04_parameter_regression`保留BGV的CF3/5加法/乘法、CF15重线性化、CF3降层，
以及三方案generator-3步长1旋转，共7项。它们是05的不同参数覆盖，参考为SEAL，不假称Poseidon API。
`05_pending_interfaces`收录Encode和Bootstrap，明确尚未接入AM，不发布占位ELF。

KeySwitch与Reline不能合并：04把c0/c1置零，隔离c2切换；05保留真实三分量密文并验证最终合并。
05内部自然会使用04的基本算子，共享指令链不代表测试接口和验收边界相同。

`src/05_poseidon_library/01_ckks`、`02_bfv`、`03_bgv`各有十个独立C文件：
以下五项分别提供N128和N4096。文件名含方案、操作和规模，目录不再加规模层。

| 测试 | CKKS接口 | BFV/BGV接口 | 检查 |
|---|---|---|---|
| HADD | `add` | `add` | 同层两密文相加，BGV初始correction factor为1 |
| HMUL_RELINE | `multiply_relin` | `multiply_relin` | 真密文乘法及重线性化，最终两分量 |
| RELINE | `relinearize` | `relinearize` | 真实三分量输入、BV评估密钥和最终合并 |
| RESCALE/MODSWITCH | `rescale` | `drop_modulus_to_next` | 降一层、取整、scale/correction factor |
| ROTATE | `rotate` | `rotate_row` | 半行旋转、Galois评估密钥及KeySwitch |

每个ELF只执行一次，不在一个用例串行塞入其它方案或规模。
CKKS/BFV使用Q4|P1，BGV使用Q3|P1、明文模数65537；密钥切换变体固定为BV。
生成的完整指令流为DLOAD/必要配置/运算/DSTORE，末尾唯一PSYNC。
AM通过MMIO等待IRQ电平有效且BUSY清零，不启用PLIC或timer。
输入、常量和评估密钥只读，输出毒化；检查全部golden及只读区/64-line guard。
沿用摘要与`_silent`两种包，不默认打印4096个系数。

## 真实库调用与AM边界

官方Poseidon是主机C++库，硬件后端还依赖其设备驱动，不能直接链接进当前裸机RISC-V AM。
本组是**库API的主机oracle与目标指令实现对拍**，不宣称直接运行Poseidon硬件驱动。

1. 主机Fixture固定输入、随机流、模数和评估密钥，不在两个库间重新随机加密。
2. 桥接显式传递Q/P、NTT根、密文分量、NTT标志、scale和correction factor；
   parms_id按同一模数链重绑定。Q/P顺序和NTT根不符会失败。
3. `PoseidonFactory`选择`DEVICE_SOFTWARE`，真实调用表中的API。
   对乘法还核对三分量tensor；除下述BFV乘法外，结果和元数据必须逐word与同输入的modified-SEAL一致。
4. inline-asm main生成同一数据/密钥下的指令流。沿用04组已验证的物理布局和rounded-P适配，
   接收器复放实际指令，必须逐word对齐golden且未破坏只读区/guard。
5. ELF再核对实际机器码和嵌入数据。目标返回0才是该次IT自检通过。

库API/软件模型通过不等于VCS通过；软件比较不使用CKKS误差容限放宽原始整数检查。
N128关闭安全等级约束，仅用于快速功能定位，不是安全部署参数；N4096也不能代替项目安全参数审查。

### BFV乘法的参考差异

实际对拍确认两库BEHZ乘法产生不同的噪声密文，不能宣称原始密文完全相同。
这两个HMUL_RELINE例在主机端分别调用Poseidon和SEAL的Decryptor，
对三分量tensor及最终两分量结果的全部N个明文系数做模t严格比较。
不使用浮点误差容限、不跳过失配；私钥仅存在主机进程内，不进入AM镜像或交付数据。
AM执行inline-asm的BEHZ实现，仍必须逐word对齐经SEAL生成且已做上述语义对拍的golden。
`POSEIDON_ORACLE.json`明确记录这一验收类型，其它28例保持原始word和元数据严格比较。
Poseidon规范项采用常规初始correction factor=1；旧3/5组合已迁入05的参数回归目录，保留原golden和检查。

## 旋转约定限制

Poseidon的槽位generator为5，SEAL/当前inline-asm为3。
不能把“步长1”的原始数组或密钥直接当成同一接口。
本组使用步长N/4（N128为32，N4096为1024）；二者的Galois元素均为N+1。
保留真实Poseidon `rotate/rotate_row`调用，并使用同一Galois密钥和逐word比较。
这覆盖半行旋转，不覆盖Poseidon任意步长旋转。旧generator-3步长1用例已迁入05参数回归，指令、输入和golden不变。
以后扩展任意步长必须在生产者支持generator-5布局及对应密钥/专用twiddle后增加，不能静默套用。

## 构建与追溯

安装`libgmp-dev`、CMake和已有RISC-V工具链，然后：

```sh
git submodule update --init --recursive
make -C tests/hputest all JOBS=4
make -C tests/hputest silent JOBS=4
```

新依赖在`third_party/poseidon`；桥接按库头文件分成独立翻译单元，
避免两库对同一`std::array<uint64_t,4>`的hash定义冲突，不修改任一submodule源码。
生成器在`tools/hpu-scheme-cases/poseidon_*.cpp`。
`scheme-cases.tsv`记录30项Poseidon规范用例及7项参数回归，现有GitHub workflow自动编译并发布到05组。
ELF/BIN/反汇编和数据只进入Actions artifact，不提交到源码仓库。

每例保留`POSEIDON_ORACLE.json`（库版本、实际API、方案、N、旋转步长、比较状态），
`upstream/`（原始交付）、`AM_ADAPTATION.json`（适配与软件门禁）、
`PROGRAM_MODEL.log`（逐指令验证）、完整指令及DMA/DDR布局。
顶层MANIFEST记录Poseidon版本。错误版本、缺失oracle、错误API或未通过比较均拒绝导入。
